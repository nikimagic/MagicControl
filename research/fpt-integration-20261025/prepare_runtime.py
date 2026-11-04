"""Copy reviewed original OEM bytes only; never execute FPT or its driver."""
from pathlib import Path
import hashlib, json, zipfile
root = Path(__file__).resolve().parents[2]
archive = root/'research/fpt-official-origin-20261025/GIGABYTE_BIOS_NHxxPNP_RCxxKE_FB05.zip'
archive_hash = '5d9e2edb0b405708eef6520c3327d3cb75d5dac0d33e65e1e14a41fc8c2b49b5'
assert hashlib.sha256(archive.read_bytes()).hexdigest() == archive_hash
member = 'BIOS_NHxxPNP(RCxxKE)FB05/ALL/FPTW64.exe'
with zipfile.ZipFile(archive) as z:
    exe = z.read(member)
driver = exe[0x24ec88:0x24ec88+43760]
files = [('FPTW64.exe', exe, '15d329ce4e5cbce10ad690e0727c54ffbbbb2a0489f80919c1b3b37858564aaa'),
         ('driver-reference.bin', driver, '74b3b7c559af70dca454486d49352bd251d68c43637b2898e4da8bcae29a447f')]
out = root/'src/platform/windows/runtime/IntelFpt'
out.mkdir(exist_ok=True)
for name, data, expected in files:
    assert hashlib.sha256(data).hexdigest() == expected
    (out/name).write_bytes(data)
provenance = dict(tool='Intel Flash Programming Tool', version='16.1.25.1871',
    source_url='https://download.gigabyte.com/FileList/BIOS/BIOS_NHxxPNP_RCxxKE_FB05.zip',
    source_archive_sha256=archive_hash, member=member,
    bytes_unmodified=True, driver_reference_offset=0x24ec88,
    files=[dict(name=n, bytes=len(d), sha256=h) for n,d,h in files],
    fixed_commands=['-I', '-d <new-output-file>'], flash_programming_requested=False,
    note='Driver reference is inert Authenticode evidence; FPT extracts its own unchanged embedded driver. No OEM scripts or BIOS image included.')
(out/'PROVENANCE.json').write_text(json.dumps(provenance, indent=2)+'\n', encoding='utf-8')
print(json.dumps(provenance, indent=2))
