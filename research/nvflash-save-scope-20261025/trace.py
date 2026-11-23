"""Trusted Ghidra reads the existing imported PE; no target executable runs."""
from pathlib import Path
import subprocess,sys,json,shutil,tempfile
root=Path(__file__).resolve().parents[2];scripts=Path(__file__).resolve().parent
trusted=root.parent.parent/'mechrevo/.tools';ghidra=trusted/'Ghidra_12.1.4/ghidra_12.1.4_PUBLIC';java=trusted/'Temurin21/jdk-21.0.12.1+1/bin/java.exe'
linux=len(sys.argv)>3 and sys.argv[3]=='linux'
targets=sys.argv[1];label=sys.argv[2]
if not label or Path(label).name!=label:raise ValueError('label must be a file name')
archive=root.parent/'Decompile/NVIDIA_NVFLASH_5_867'/('Linux' if linux else 'Windows')
project='NvflashLinuxSavedFile' if linux else 'NvflashSavedFile'
runtime=root/'out/nvflash-research';runtime.mkdir(parents=True,exist_ok=True)
work=Path(tempfile.mkdtemp(prefix='linux-trace-' if linux else 'windows-trace-',dir=runtime))
shutil.copy2(archive/(project+'.gpr'),work/(project+'.gpr'))
shutil.copytree(archive/(project+'.rep'),work/(project+'.rep'))
out=work/'result';out.mkdir()
args=[str(java),'-Djava.awt.headless=true','-Djava.system.class.loader=ghidra.GhidraClassLoader','-Duser.home='+str(work),'-Dapplication.settingsdir='+str(work/'settings'),'-Dapplication.cachedir='+str(work/'cache'),'-Dapplication.tempdir='+str(work/'temp'),'-Xmx4G','-cp',str(ghidra/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(work),project,'-process','nvflash-linux-x64.reference.bin' if linux else 'nvflash64.exe.reference.bin','-noanalysis','-readOnly','-scriptPath',str(scripts),'-postScript','TraceSave.java',str(out/label),targets]
with (out/(label+'.log')).open('w',encoding='utf8') as log:r=subprocess.run(args,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=600)
print(json.dumps(dict(exit=r.returncode,label=label,target_executed=False,output=str(out))))
