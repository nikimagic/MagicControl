"""Copy hash-pinned original OEM files without executing them."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[2];source=root.parent/'analysis/bios-reader-ultra-20261025'
out=root/'src/platform/windows/runtime/IntelFpt19';out.mkdir(exist_ok=True)
files=[('FPTW64.exe',source/'Intel-FPT-19.0.5.1948/FPTW64.exe','88491119862830235854cd29b9857de21870ac4dc16f74544b1a665f64d62807'),
       ('driver-reference.bin',source/'Intel-FPT-19.0.5.1948/embedded-driver-resource-1.bin','acd5d366a73e9d60c654f9b6d76397d0439606939d6b0c7ee24c4f397452899a')]
for name,path,expected in files:
    data=path.read_bytes();assert hashlib.sha256(data).hexdigest()==expected;(out/name).write_bytes(data)
archive=source/'iGameCenter-2.0.0.82-Installer_Prod.exe'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='ce7f0b697dbc64662541838aed2d914508bb085db4b8e6186749b0dd4a4f8c53'
provenance=dict(tool='Intel Flash Programming Tool',version='19.0.5.1948',
    source_page='https://www.colorful.cn/igamecenter/',
    source_url='https://download.colorful.cn/iGameCenter/iGameCenter-2.0.0.82-2026.09.12-Installer_Prod.exe',
    source_archive_sha256='ce7f0b697dbc64662541838aed2d914508bb085db4b8e6186749b0dd4a4f8c53',
    member='app/iGameAPI/Notebook/NotebookSDK/N15_25/FPT/FPTW64_8102.exe',
    original_filename='FPTW64_8102.exe',bytes_unmodified=True,
    driver_reference_resource=[10,118,1033],driver_version='2508.0.23.0',
    files=[dict(name=name,bytes=path.stat().st_size,sha256=expected) for name,path,expected in files],
    fixed_commands=['-I','-d <new-output-file>'],flash_programming_requested=False,
    scope='Actual HYDROC-16 G2 or YAOSHI X6AR55xY, GenuineIntel Ultra9 275HX, reviewed LPC and successful FPT -I',
    note='Driver reference is inert Authenticode evidence. Vendor tool extracts its own embedded driver. No OEM installer, firmware payload or flash scripts are bundled.')
(out/'PROVENANCE.json').write_text(json.dumps(provenance,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(files=len(files),hardware_executed=False)))
