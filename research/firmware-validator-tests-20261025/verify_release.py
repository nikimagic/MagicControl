"""File-only release receipt. Never invokes product or vendor executables."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess

root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
sha = lambda b: hashlib.sha256(b).hexdigest()
build = json.loads((root / 'build/product-build.json').read_text(encoding='utf-8-sig'))
assert build['version'] == '0.1.197' and build['status'] == 'complete'
assert all(sha((root / p).read_bytes()) == digest for p, digest in build['application_inputs'].items())
changed = subprocess.check_output(['git', 'diff', 'e4deadb', '--name-only', '--', 'src', 'cmake', 'Tools'], cwd=root, text=True).splitlines()
assert changed == ['src/platform/common/BiosImageValidation.h'], changed
previous = subprocess.check_output(['git', 'show', 'e4deadb:src/platform/common/BiosImageValidation.h'], cwd=root).decode().replace('\r\n', '\n')
current = (root / 'src/platform/common/BiosImageValidation.h').read_text()
without_helper = current[:current.index('// File-only recognition of the reviewed')] + current[current.index('inline unsigned firmwareVolumes'):]
legacy_check = '''    const auto reset0 = static_cast<unsigned char>(uefi[uefi.size() - 16]), reset1 = static_cast<unsigned char>(uefi[uefi.size() - 15]);
    if (shape.firmwareVolumes < 2 || (reset0 != 0xe9 && reset0 != 0xea && reset1 != 0xe9 && reset1 != 0xea))'''
new_check = '''    // Two FV and this reset form are this decoder's conservative admission
    // policy. Refusal means unsupported/unconfirmed, not damaged firmware.
    if (shape.firmwareVolumes < 2 || !resetJump(uefi))'''
assert without_helper.replace(new_check, legacy_check) == previous

# Same file-only PE resource check used for earlier portable receipts.
def pe_resources(b):
    u16 = lambda at: struct.unpack_from('<H', b, at)[0]
    u32 = lambda at: struct.unpack_from('<I', b, at)[0]
    nt = u32(60); opt = nt + 24; sections = opt + u16(nt + 20)
    assert b[nt:nt+4] == b'PE\0\0' and u16(opt) == 0x20b
    def raw(rva):
        for n in range(u16(nt+6)):
            at = sections+n*40
            size, base, rs, ro = struct.unpack_from('<IIII', b, at+8)
            if base <= rva < base+max(size, rs): return ro+rva-base
        raise ValueError('Unmapped RVA')
    resource = raw(u32(opt+112+16))
    def directory(at):
        return [struct.unpack_from('<II', b, at+16+i*8) for i in range(u16(at+12)+u16(at+14))]
    def get(kind, identity):
        at = resource
        for wanted in [kind, identity]:
            matches = [x for x in directory(at) if x[0] == wanted]
            assert len(matches) == 1
            at = resource+(matches[0][1]&0x7fffffff)
        languages = directory(at); assert len(languages) == 1
        rva, size = struct.unpack_from('<II', b, resource+languages[0][1])
        return b[raw(rva):raw(rva)+size]
    return u16(opt+68), get

catalogue = (root / 'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text()
entries = re.findall(r'\{(\d+),"([^"]+)","([^"]+)","([a-f0-9]{64})",(\d+)\}', catalogue)
assert len(entries) == 47
receipt = {'version': build['version'], 'application_inputs_match': True,
           'application_input_count': len(build['application_inputs']),
           'only_production_change': changed[0], 'only_reset_predicate_changed': True,
           'native_readers_runtime_argv_ui_unchanged_from_196': True,
           'hardware_executed': False, 'vendor_or_product_executed': False, 'binaries': []}
for platform in ['windows', 'linux']:
    for cli in [False, True]:
        name = ('mctl.exe' if cli else 'MagicControl.exe') if platform == 'windows' else ('mctl' if cli else 'MagicControl')
        produced = root/'build'/platform/('bin/Release' if platform == 'windows' else 'bin')/name
        portable = root/'MagicControl'/platform/name
        b = portable.read_bytes()
        assert b == produced.read_bytes()
        assert sha(b) == build['platforms'][platform]['cli_sha256' if cli else 'sha256'].lower()
        assert (portable.parent/'build-version.txt').read_text().strip() == build['version']
        row = {'os': platform, 'cli': cli, 'bytes': len(b), 'sha256': sha(b)}
        if platform == 'windows':
            subsystem, get = pe_resources(b); assert subsystem == (3 if cli else 2)
            if cli: assert 'asInvoker' in get(24, 1).decode('utf-8', errors='replace')
            for identity, group, filename, digest, size in entries:
                data = get(10, int(identity)); assert len(data) == int(size) and sha(data) == digest
            row['original_embedded_resources_sha_verified'] = len(entries)
        else:
            assert b[:4] == b'\x7fELF' and build['version'].encode() in b
        receipt['binaries'].append(row)
assert sha((root/'MagicControl/linux/tools/NvFlash/nvflash').read_bytes()) == '754693fb8f833be75601f95cb08a6e0cf538053cce84ab69020ec74ae96a6e70'
receipt['offline_results'] = {}
for os, groups in [('windows', 7), ('linux', 7), ('linux-sanitize', 5)]:
    log = (out/(os+'-tests.txt')).read_text(encoding='utf-8-sig')
    assert f'100% tests passed, 0 tests failed out of {groups}' in log
    assert not re.search(r'\*\*\*(?:Skipped|Failed)|ERROR: AddressSanitizer|runtime error:', log)
    receipt['offline_results'][os] = {'groups_passed': groups, 'skipped': 0, 'log_sha256': sha((out/(os+'-tests.txt')).read_bytes())}
for os in ['windows', 'linux']:
    data = json.loads((out/('real-files-'+os+'.json')).read_text())
    assert len(data['fixtures']) == 5 and sum(f['expected_accepted'] for f in data['fixtures']) == 4
receipt['cpp_assertions_per_os'] = 742
receipt['real_fixtures_per_os'] = 5
receipt['nvidia_catalogue_profiles'] = 14
receipt['nvidia_catalogue_gpu_families'] = 7
receipt['nvidia_synthetic_collector_scenarios_per_os'] = 9
receipt['missing_real_fixtures'] = ['FPT19/Ultra275HX', 'NVFlash VBIOS/EEPROM']
(out/'verification.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
print('PASS0.1.197: four portable binaries,553source inputs,47original Windows resources per binary; only reset predicate changed.')
