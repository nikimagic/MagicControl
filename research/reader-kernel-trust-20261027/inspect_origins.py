"""Recheck official archive provenance and embedded driver byte identity."""
from pathlib import Path
import hashlib,json,zipfile
OUT=Path(__file__).resolve().parent;ROOT=OUT.parents[1]
sha=lambda b:hashlib.sha256(b).hexdigest()
runtime=ROOT/'src/platform/windows/runtime'
records=[]
p=json.loads((runtime/'IntelFpt/PROVENANCE.json').read_text())
archive=ROOT/'research/fpt-official-origin-20261025/GIGABYTE_BIOS_NHxxPNP_RCxxKE_FB05.zip'
with zipfile.ZipFile(archive) as z:
    exe=z.read(p['member'])
driver=(runtime/'IntelFpt/driver-reference.bin').read_bytes()
records.append(dict(package='IntelFpt',source_url=p['source_url'],archive_sha256=sha(archive.read_bytes()),
                    archive_matches_provenance=sha(archive.read_bytes())==p['source_archive_sha256'],
                    exe_sha256=sha(exe),exe_matches_bundled=exe==(runtime/'IntelFpt/FPTW64.exe').read_bytes(),
                    driver_sha256=sha(driver),embedded_driver_offset=exe.find(driver),driver_matches_bundled=exe.find(driver)>=0))
p=json.loads((runtime/'IntelFpt19/PROVENANCE.json').read_text())
base=ROOT.parent/'analysis/bios-reader-ultra-20261025'
archive=base/'iGameCenter-2.0.0.82-Installer_Prod.exe'
exe=(base/'extracted'/p['member']).read_bytes()
driver=(runtime/'IntelFpt19/driver-reference.bin').read_bytes()
records.append(dict(package='IntelFpt19',source_url=p['source_url'],archive_sha256=sha(archive.read_bytes()),
                    archive_matches_provenance=sha(archive.read_bytes())==p['source_archive_sha256'],
                    exe_sha256=sha(exe),exe_matches_bundled=exe==(runtime/'IntelFpt19/FPTW64.exe').read_bytes(),
                    driver_sha256=sha(driver),embedded_driver_offset=exe.find(driver),driver_matches_bundled=exe.find(driver)>=0))
(OUT/'INTEL-ORIGINS.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
for r in records:
    print(r['package'],'official archive identity',r['archive_matches_provenance'],'EXE matches',r['exe_matches_bundled'],'embedded driver matches',r['driver_matches_bundled'])
    if not all(r[x] for x in ('archive_matches_provenance','exe_matches_bundled','driver_matches_bundled')):raise SystemExit(1)
