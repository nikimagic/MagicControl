"""Analyze saved Wujie AFU files only; no hardware or vendor-tool execution.

Format sources and their hashes are in sources/INDEX.json. This is a
research decoder, not an admission/writer added to the production program.
It deliberately separates structural coverage from physical SPI completeness.
"""
from pathlib import Path
import collections
import hashlib
import json
import re
import struct
import zlib

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[1]
CAPTURE = ROOT / 'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/bios-dumps/bios-20261029-054541-0'
EARLY = ROOT / 'research/windows-full-spi-20261024/runs/afu-20261024-185128471/bios-32MiB.bin'
data = (CAPTURE / 'bios.bin').read_bytes()
verify = (CAPTURE / 'bios-verify.bin').read_bytes()
assert len(data) == 0x2000000 and data == verify
assert data == (OUT / 'bios.bin').read_bytes()
sha = lambda b: hashlib.sha256(b).hexdigest()
u32 = lambda at: struct.unpack_from('<I', data, at)[0]


def fletcher32(b):
    assert len(b) % 2 == 0
    lo = hi = 0xffff
    words = struct.unpack('<' + 'H' * (len(b) // 2), b)
    for start in range(0, len(words), 359):
        for w in words[start:start + 359]:
            lo += w
            hi += lo
        lo = (lo & 0xffff) + (lo >> 16)
        hi = (hi & 0xffff) + (hi >> 16)
    lo = (lo & 0xffff) + (lo >> 16)
    hi = (hi & 0xffff) + (hi >> 16)
    return ((hi << 16) | lo) & 0xffffffff


def summary(start, size):
    assert 0 <= start <= len(data) and 0 <= size <= len(data) - start
    b = data[start:start + size]
    return dict(offset=start, bytes=size, end_exclusive=start + size,
                sha256=sha(b), ff_bytes=b.count(255), zero_bytes=b.count(0),
                uniform=len(set(b)) <= 1)


names = {}
for p in (OUT / 'sources').glob('coreboot-*directory.h'):
    names.update({int(h, 16): n for n, h in re.findall(r'(AMD_\w+)\s*=\s*(0x[0-9a-fA-F]+)', p.read_text())})

directories = {}
for magic in (b'$PSP', b'$PL2', b'$BHD', b'$BL2'):
    for match in re.finditer(re.escape(magic), data):
        at = match.start()
        if at + 16 > len(data):
            continue
        count, info = u32(at + 8), u32(at + 12)
        stride = 24 if magic in (b'$BHD', b'$BL2') else 16
        end = at + 16 + stride * count
        if not 1 <= count <= 512 or end > len(data):
            continue
        checksum = fletcher32(data[at + 8:end])
        if checksum != u32(at + 4):
            continue  # Firmware code also contains these byte strings.
        mode = (info >> (24 if info >> 31 else 29)) & 3
        directories[at] = dict(offset=at, magic=magic.decode(), count=count,
                               checksum=checksum, checksum_ok=True, version=info >> 31,
                               directory_address_mode=mode, bytes=end - at, entries=[])


def address(raw, directory):
    entry_mode, value = raw >> 62, raw & ((1 << 62) - 1)
    mode = entry_mode if directory['directory_address_mode'] in (2, 3) else directory['directory_address_mode']
    if mode == 0:
        if value < len(data):
            return value
        if (1 << 32) - len(data) <= value < (1 << 32):
            return value - ((1 << 32) - len(data))
        raise ValueError('Unresolved physical address')
    if mode == 1:
        return value
    if mode == 2:
        return directory['offset'] + value
    raise ValueError('Unreviewed relative-to-slot address')


slots = {}
for directory in directories.values():
    bios = directory['magic'] in ('$BHD', '$BL2')
    for i in range(directory['count']):
        at = directory['offset'] + 16 + i * (24 if bios else 16)
        flags, size, raw = struct.unpack_from('<IIQ', data, at)
        typ = flags & 255
        e = dict(index=i, type=typ, flags=flags, declared_size=size,
                 raw_address=raw, name=names.get(typ, 'unknown'))
        if not bios and typ == 0x0b:
            e.update(kind='fuse_value_not_flash_pointer')
        elif bios and typ == 0x61:
            assert size == 0
            e.update(kind='RAM_APOB_target_not_flash_range', destination=struct.unpack_from('<Q', data, at + 16)[0])
        else:
            offset = address(raw, directory)
            if not bios and typ in (0x48, 0x4a):
                # Image Slot Header, coreboot ish.h; size zero is not an empty image.
                block = data[offset:offset + 32]
                assert len(block) == 32
                stored = struct.unpack_from('<I', block)[0]
                calculated = fletcher32(block[4:])
                target = struct.unpack_from('<I', block, 16)[0]
                assert target in directories and stored == calculated
                slots[offset] = dict(offset=offset, bytes=32, checksum=stored, checksum_ok=True,
                                     pl2_offset=target, psp_id=struct.unpack_from('<I', block, 20)[0])
                e.update(kind='image_slot_header', target_directory=target, **summary(offset, 32))
            elif bios and ((flags >> 16) & 8):
                # Directory size is decompressed size, not the stored flash extent.
                stored_size = u32(offset + 0x14)
                assert 0 < stored_size <= len(data) - offset - 0x100
                stream = zlib.decompressobj()
                uncompressed = stream.decompress(data[offset + 0x100:offset + 0x100 + stored_size], 64 * 1024 * 1024)
                assert stream.eof and not stream.unused_data and len(uncompressed) == size
                e.update(kind='compressed_bios', decompressed_bytes=len(uncompressed),
                         decompressed_sha256=sha(uncompressed), zlib_verified=True,
                         **summary(offset, 0x100 + stored_size))
            else:
                e.update(kind='flash_range', **summary(offset, size))
                if typ in (0x40, 0x49, 0x70):
                    assert offset in directories
                    e['target_directory'] = offset
        directory['entries'].append(e)

efs = 0x20000
assert u32(efs) == 0x55aa55aa
root_offsets = [u32(efs + 0x14), u32(efs + 0x2c)]
assert root_offsets == [0x81000, 0x82000]
reachable, pending = set(), root_offsets[:]
while pending:
    at = pending.pop()
    if at in reachable:
        continue
    reachable.add(at)
    pending += [e['target_directory'] for e in directories[at]['entries'] if 'target_directory' in e]
assert reachable == set(directories)

volumes = []
for m in re.finditer(b'_FVH', data):
    at = m.start() - 0x28
    if at < 0 or at + 0x38 > len(data):
        continue
    length = struct.unpack_from('<Q', data, at + 0x20)[0]
    header = struct.unpack_from('<H', data, at + 0x30)[0]
    if not (0x38 <= header <= 4096 and header % 2 == 0 and header <= length <= len(data) - at):
        continue
    checksum = sum(struct.unpack('<' + 'H' * (header // 2), data[at:at + header])) & 0xffff
    assert checksum == 0
    volumes.append(dict(offset=at, bytes=length, header_bytes=header, checksum_ok=True))

report = (OUT / 'bios.bin.report.txt').read_text()
rows = [[c.strip() for c in line.split('|')] for line in report.splitlines()]
rows = [r for r in rows if len(r) >= 6 and re.fullmatch('[0-9A-Fa-f]{8}', r[2])]
nv = []
for start in (0x19c0000, 0x19e0000):
    records = [r for r in rows if r[0] == 'NVAR entry' and start <= int(r[2], 16) < start + 0x20000]
    assert records
    sizes = []
    for r in records:
        if len(r) > 6 and r[6] == 'SPIROMSIZE' and r[1] == 'Full':
            at = int(r[2], 16)
            length = struct.unpack_from('<H', data, at + 4)[0]
            attributes = data[at + 9]
            pos = at + 10 + (16 if attributes & 4 else 1)
            if attributes & 2:
                pos = data.index(0, pos, at + length) + 1
            else:
                while data[pos:pos + 2] != b'\0\0':
                    pos += 2
                pos += 2
            value = int.from_bytes(data[pos:pos + 4], 'little')
            assert value == len(data)
            sizes.append(dict(offset=at, value=value))
    known = sorted({r[6] for r in records if len(r) > 6 and r[1] in ('Full', 'Link') and
                    r[6] in {'Setup', 'AmdSetup', 'UniWillVariable', 'SPIROMSIZE', 'BootOrder', 'Boot0000', 'PK', 'KEK', 'db', 'dbx'}})
    nv.append(dict(**summary(start, 0x20000), records_including_nested_defaults=len(records),
                   parser_subtypes=dict(collections.Counter(r[1] for r in records)),
                   recognized_variable_names=known, SPIROMSIZE=sizes))
    assert sizes

flat = [e for d in directories.values() for e in d['entries']]
nv_refs = [e for d in directories.values() if d['magic'] == '$BL2' for e in d['entries'] if e['type'] == 0x6d]
assert all(any(v['offset'] == e['offset'] and v['bytes'] == e['bytes'] for v in nv) for e in nv_refs if e['offset'] in (0x19c0000, 0x19e0000))
earlier = EARLY.read_bytes()
changed_pages = [off for off in range(0, len(data), 4096) if data[off:off + 4096] != earlier[off:off + 4096]]
mutable = [(0x21000, 0x41000), (0x41000, 0x81000), (0x19c0000, 0x19e0000), (0x19e0000, 0x1a00000)]
assert all(any(start <= off < end for start, end in mutable) for off in changed_pages)
# Coarse page attribution is not enough: check every differing byte as well.
outside_mutable_equal = all(data[a:b] == earlier[a:b] for a, b in
    [(0, 0x21000), (0x81000, 0x19c0000), (0x1a00000, len(data))])
assert outside_mutable_equal
compressed_offsets = sorted({e['offset'] for e in flat if e['kind'] == 'compressed_bios'})
result = dict(scope='Saved-file structural analysis only; no hardware IO or vendor-reader execution',
              capture=dict(path=str(CAPTURE / 'bios.bin'), bytes=len(data), sha256=sha(data), equal_second_pass=True),
              EFS=dict(offset=efs, signature='55AA55AA', psp_roots=root_offsets),
              directories=list(directories.values()), image_slots=list(slots.values()),
              reachable_directories=sorted(reachable), firmware_volumes=volumes, nvram=nv,
              compressed_bios=[e for e in flat if e['kind'] == 'compressed_bios'],
              nvram_directory_references=nv_refs,
              comparison_20261024=dict(path=str(EARLY), sha256=sha(earlier), changed_4KiB_pages=changed_pages,
                                       lower_16MiB_equal=data[:0x1000000] == earlier[:0x1000000],
                                       all_differences_inside_declared_NV_ranges=True,
                                       outside_declared_NV_ranges_byte_identical=outside_mutable_equal),
              distinct_compressed_bios_offsets=compressed_offsets,
              verdict=dict(structural_32MiB_map_consistent=True, psp_present=True, bios_present=True, nvram_present=True,
                           physical_spi_complete=None, reserved_gap_bytes_not_independently_attested=True,
                           separate_live_ec_flash_captured=False, firmware_signatures_cryptographically_verified=False))
(OUT / 'evidence.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(dict(bytes=len(data), sha256=sha(data), directories=len(directories),
                     image_slots=len(slots), directory_entries=len(flat), firmware_volumes=len(volumes),
                     compressed_bios=len(result['compressed_bios']), nvram_stores=len(nv),
                     changed_pages_versus_24=changed_pages,
                     verdict=result['verdict']), indent=2))
