"""Trusted Ghidra parses an inert saved PE. Never runs NVFlash or its driver."""
from pathlib import Path
import subprocess,json,hashlib
root=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent
tool=root.parent/'analysis/nvflash-research-20261025/mirror-extracted/nvflash64.exe.reference.bin'
assert hashlib.sha256(tool.read_bytes()).hexdigest()=='c0620a7be0ffdad3782d9ddf238d1a19c31b587cbcfee7036819c8a50e2d89cd'
trusted=root.parent.parent/'mechrevo/.tools';ghidra=trusted/'Ghidra_12.1.4/ghidra_12.1.4_PUBLIC';java=trusted/'Temurin21/jdk-21.0.12.1+1/bin/java.exe'
work=root/'build/nvflash-static-20261025';work.mkdir(parents=True,exist_ok=True)
targets=['141139930','141140cf8','141198ba0','141198cb0','141199a08','141199a28','141199a80','141199cb0','141199cd8','1411b0418','1411b0458','1411cdd60','1411d3500','1411d35a0','1411d80c0','1411d83b8','1411df240','1411e7368']
args=[str(java),'-Djava.awt.headless=true','-Djava.system.class.loader=ghidra.GhidraClassLoader','-Duser.home='+str(work),'-Dapplication.settingsdir='+str(work/'settings'),'-Dapplication.cachedir='+str(work/'cache'),'-Dapplication.tempdir='+str(work/'temp'),'-Xmx4G','-cp',str(ghidra/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(work),'NvflashSavedFile','-import',str(tool),'-overwrite','-analysisTimeoutPerFile','240','-max-cpu','2','-scriptPath',str(out),'-postScript','ReviewNvflash.java',str(out/'decompiled'),','.join(targets)]
with (out/'ghidra-console.txt').open('w',encoding='utf-8') as log:
    r=subprocess.run(args,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=600)
print(json.dumps(dict(exit=r.returncode,hardware_executed=False)))
