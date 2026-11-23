"""Read saved Intel FPT/GM6 SPI files only. Never execute FPT or its driver."""
from pathlib import Path
import bisect, collections, hashlib, json, re, struct, sys, zipfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'research/firmware-reader-static-support'))
from runtime import new_run,publish_selection
OUT = new_run(ROOT,'fpt-inspection')
SCRATCH = OUT / 'scratch'
SCRATCH.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT.parent.parent / 'mechrevo/.tools/pydeps'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
sha = lambda b: hashlib.sha256(b).hexdigest()
file = ROOT.parent / 'BIOS Unlock/FPTW64.exe'
b = file.read_bytes()
assert sha(b) == '15d329ce4e5cbce10ad690e0727c54ffbbbb2a0489f80919c1b3b37858564aaa'
pe = struct.unpack_from('<I', b, 60)[0]
opt = pe + 24
image_base = struct.unpack_from('<Q', b, opt + 24)[0]
optional_size = struct.unpack_from('<H', b, pe + 20)[0]
sections = []
for index in range(struct.unpack_from('<H', b, pe + 6)[0]):
    pos = opt + optional_size + index * 40
    size, rva, raw_size, raw = struct.unpack_from('<IIII', b, pos + 8)
    sections.append(dict(name=b[pos:pos+8].split(b'\0')[0].decode(), rva=rva,
                         bytes=size, raw=raw, raw_bytes=raw_size))
def raw_from_rva(rva):
    s = next(s for s in sections if s['rva'] <= rva < s['rva'] + s['raw_bytes'])
    return s['raw'] + rva - s['rva']
def va_from_raw(offset):
    s = next(s for s in sections if s['raw'] <= offset < s['raw'] + s['raw_bytes'])
    return image_base + s['rva'] + offset - s['raw']

exception_rva, exception_size = struct.unpack_from('<II', b, opt + 112 + 3 * 8)
exception_raw = raw_from_rva(exception_rva)
functions = [struct.unpack_from('<III', b, at) for at in range(exception_raw, exception_raw + exception_size, 12)]
starts = [image_base + f[0] for f in functions]
def function_at(va):
    i = bisect.bisect_right(starts, va) - 1
    if i >= 0 and va < image_base + functions[i][1]:
        return functions[i][0]

strings = []
for m in re.finditer(rb'[\x20-\x7e]{3,}', b):
    try: va = hex(va_from_raw(m.start()))
    except StopIteration: va = None
    strings.append(dict(raw=hex(m.start()), va=va, text=m.group().decode()))
interesting = [s for s in strings if re.search(r'(?i)dumpflash|dump flash|memory dump|read access|read.access|Flash Descriptor|WNPDriver|IntelCseWNP|supported platform|Alder Lake|Raptor Lake|Reading Flash|writing flash contents|operation passed', s['text'])]
by_va = {int(s['va'], 16): s for s in interesting if s['va']}
refs = []
pci_hits = []
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
md.skipdata = True
for s in sections:
    if s['name'] != '.text':
        continue
    for ins in md.disasm(b[s['raw']:s['raw']+s['raw_bytes']], image_base+s['rva']):
        if ins.id == 0: continue
        for op in ins.operands:
            target = None
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                target = ins.address + ins.size + op.mem.disp
            elif op.type == X86_OP_IMM:
                target = op.imm
                if op.imm in (0x7a0c, 0x7a00, 0x7a80, 0x7a68, 0x7aa4, 0x7a24, 0x7ae0):
                    pci_hits.append(dict(va=hex(ins.address), instruction=ins.mnemonic+' '+ins.op_str,
                                         function_rva=hex(function_at(ins.address) or 0)))
            if target in by_va:
                refs.append(dict(va=hex(ins.address), instruction=ins.mnemonic+' '+ins.op_str,
                                 function_rva=hex(function_at(ins.address) or 0), string=by_va[target]))
for s in interesting:
    if not s['va']: continue
    needle = struct.pack('<Q', int(s['va'],16))
    s['absolute_pointer_raw_offsets'] = [hex(m.start()) for m in re.finditer(re.escape(needle), b)]

# A driver is extracted as inert .bin for PE/signature analysis, never installed.
embedded = b[0x24ec88:0x24ec88+43760]
assert embedded.startswith(b'MZ')
(SCRATCH / 'embedded-driver.bin').write_bytes(embedded)
images = []
for name, path in [('stock', ROOT/'research/ec-platform-catalogue-20261023/new-backup-20261023/backup.fd'),
                   ('patched', ROOT/'research/ub23/new.fd')]:
    data = path.read_bytes()
    assert data[16:20] == bytes.fromhex('5aa5f00f')
    flmap0, flmap1, flmap2 = struct.unpack_from('<III', data, 20)
    frba, fmba, fcba = ((flmap0 >> 16) & 255)*16, (flmap1 & 255)*16, (flmap0 & 255)*16
    masters = list(struct.unpack_from('<6I', data, fmba))
    names = {0:'Descriptor',1:'BIOS',2:'ME',3:'GbE',4:'PlatformData',5:'DeviceExpansion1',6:'BIOS2',8:'EC',9:'DeviceExpansion2',10:'IE',11:'10GbE0',12:'10GbE1',15:'PTT'}
    regions = []
    for index in range(16):
        value = struct.unpack_from('<I', data, frba+index*4)[0]
        base, limit = (value & 32767)*4096, ((value>>16) & 32767)*4096 + 4095
        enabled = limit >= base
        region = dict(index=index, name=names.get(index, 'Reserved'), register=hex(value),
                      base=hex(base), limit=hex(limit), enabled=enabled)
        if enabled:
            payload = data[base:limit+1]
            region.update(bytes=len(payload), sha256=sha(payload), uniform_ff=all(x==255 for x in payload),
                          host_read=bool(masters[0] & (1 << (8+index if index<12 else index-12))),
                          host_write=bool(masters[0] & (1 << (20+index if index<12 else index-8))))
        regions.append(region)
    manifests = []
    for m in re.finditer(re.escape(b'$MN2'), data[0x3000:0x3dd000]):
        tag=m.start()+0x3000;start=tag-0x1c
        header_length=struct.unpack_from('<I',data,start+4)[0]*4
        vendor=struct.unpack_from('<I',data,start+0x10)[0]
        if vendor==0x8086 and 0x80<=header_length<=4096:
            manifests.append({'tag_spi_offset':hex(tag),'header_bytes':header_length,
                              'version':'.'.join(map(str,struct.unpack_from('<4H',data,tag+8))),
                              'signature_validated':False})
    flcomp=struct.unpack_from('<I',data,fcba)[0]
    component_count=((flmap0>>8)&3)+1
    images.append(dict(name=name, path=str(path), bytes=len(data), sha256=sha(data),
                       frba=hex(frba), fmba=hex(fmba), fcba=hex(fcba),
                       flmap0=hex(flmap0), flmap1=hex(flmap1), flmap2=hex(flmap2),
                       flcomp=hex(flcomp),component_count=component_count,
                       component1_density_code=flcomp&15,component1_declared_bytes=(512*1024)<<(flcomp&15),
                       component_section_hex=data[fcba:fcba+16].hex(' '),
                       masters=[hex(x) for x in masters], regions=regions,
                       descriptor_sha256=sha(data[:4096]),me_manifests=manifests,
                       gbe_first_bank_checksum=hex(sum(struct.unpack_from('<64H',data,0x1000))&0xffff)))

platforms=[]
for index in range(31):
    at=0x172ec0+index*0xd0;low,high=struct.unpack_from('<HH',b,at)
    platforms.append({'index':index,'raw_offset':hex(at),'low':hex(low),'high':hex(high),
                      'name':b[at+4:at+0xcc].split(b'\0')[0].decode(),
                      'internal_platform':struct.unpack_from('<I',b,at+0xcc)[0],
                      'matches_gm6_saved_lpc_7a0c':low<=0x7a0c<=high})

with zipfile.ZipFile(ROOT/'Users_logs/logs_GM6IR0C (15).zip') as archive:
    text = archive.read('logs_GM6IR0C/dumps/dump-20261024-171634-0/inventory.json').decode('utf-8-sig')
    ids = sorted(set(re.findall(r'PCI\\\\VEN_8086&DEV_([0-9A-F]{4})', text)))

result = dict(static_only=True, hardware_executed=False, path=str(file), bytes=len(b), sha256=sha(b),
              image_base=hex(image_base), sections=sections, pdata_functions=len(functions),
              relevant_strings=interesting, string_code_refs=refs, pci_immediate_hits=pci_hits,
              embedded_driver=dict(raw_offset='0x24ec88', bytes=len(embedded), sha256=sha(embedded), inert_file=str(SCRATCH/'embedded-driver.bin')),
              images=images, saved_gm6_intel_pci_ids=ids,supported_lpc_table=platforms,
              supported_lpc_lookup_function_rva='0x2b7f0',supported_lpc_table_va='0x1401742c0',
              full_dump_function_rva='0x33b0',read_wrapper_function_rva='0xf320',read_engine_function_rva='0xfae0')
(OUT/'EVIDENCE.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
(OUT/'fpt-strings.txt').write_text('\n'.join(s['raw']+' '+str(s['va'])+' '+s['text'] for s in strings)+'\n', encoding='utf-8')
selected = sorted(set(int(r['function_rva'], 16) for r in refs if r['function_rva']!='0x0'))
(OUT/'selected-functions.txt').write_text(','.join(f'{x:x}' for x in selected)+'\n', encoding='utf-8')
publish_selection(OUT)
print(json.dumps(dict(output=str(OUT),sha256=sha(b), functions=len(functions), relevant_strings=len(interesting), code_refs=len(refs),
                     selected_functions=[hex(x) for x in selected], pci_hits=pci_hits,
                     host_flmstr1=[i['masters'][0] for i in images],
                     enabled_regions=[[r for r in i['regions'] if r['enabled']] for i in images],
                     saved_gm6_intel_pci_ids=ids), indent=2))
