"""Trusted Ghidra imports an inert ELF; no target program or driver execution."""
from pathlib import Path
import subprocess,json,hashlib,tempfile
root=Path(__file__).resolve().parents[2];scripts=Path(__file__).resolve().parent
tool=root/'src/platform/linux/runtime/NvFlash/nvflash'
assert hashlib.sha256(tool.read_bytes()).hexdigest()=='754693fb8f833be75601f95cb08a6e0cf538053cce84ab69020ec74ae96a6e70'
trusted=root.parent.parent/'mechrevo/.tools';ghidra=trusted/'Ghidra_12.1.4/ghidra_12.1.4_PUBLIC';java=trusted/'Temurin21/jdk-21.0.12.1+1/bin/java.exe'
runtime=root/'out/nvflash-research';runtime.mkdir(parents=True,exist_ok=True)
work=Path(tempfile.mkdtemp(prefix='linux-import-',dir=runtime));out=work/'result';out.mkdir()
targets=','.join('d:'+x['va'][2:] for x in json.loads((scripts/'linux-targets.json').read_text()))
args=[str(java),'-Djava.awt.headless=true','-Djava.system.class.loader=ghidra.GhidraClassLoader','-Duser.home='+str(work),'-Dapplication.settingsdir='+str(work/'settings'),'-Dapplication.cachedir='+str(work/'cache'),'-Dapplication.tempdir='+str(work/'temp'),'-Xmx3G','-cp',str(ghidra/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(work),'NvflashLinuxSavedFile','-import',str(tool),'-overwrite','-analysisTimeoutPerFile','180','-max-cpu','2','-scriptPath',str(scripts),'-postScript','TraceSave.java',str(out/'linux-phase1'),targets]
with (out/'linux-import.log').open('w',encoding='utf8') as log:r=subprocess.run(args,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=600)
print(json.dumps(dict(exit=r.returncode,target_executed=False,output=str(out))))
