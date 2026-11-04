"""Trusted Ghidra reads the existing imported PE; no target executable runs."""
from pathlib import Path
import subprocess,sys,json
root=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent
trusted=root.parent.parent/'mechrevo/.tools';ghidra=trusted/'Ghidra_12.1.4/ghidra_12.1.4_PUBLIC';java=trusted/'Temurin21/jdk-21.0.12.1+1/bin/java.exe'
linux=len(sys.argv)>3 and sys.argv[3]=='linux'
work=root/('build/nvflash-linux-static-20261025' if linux else 'build/nvflash-static-20261025')
targets=sys.argv[1];label=sys.argv[2]
args=[str(java),'-Djava.awt.headless=true','-Djava.system.class.loader=ghidra.GhidraClassLoader','-Duser.home='+str(work),'-Dapplication.settingsdir='+str(work/'settings'),'-Dapplication.cachedir='+str(work/'cache'),'-Dapplication.tempdir='+str(work/'temp'),'-Xmx4G','-cp',str(ghidra/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(work),'NvflashLinuxSavedFile' if linux else 'NvflashSavedFile','-process','nvflash-linux-x64.reference.bin' if linux else 'nvflash64.exe.reference.bin','-noanalysis','-readOnly','-scriptPath',str(out),'-postScript','TraceSave.java',str(out/label),targets]
with (out/(label+'.log')).open('w',encoding='utf8') as log:r=subprocess.run(args,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=600)
print(json.dumps(dict(exit=r.returncode,label=label,target_executed=False)))
