"""Trusted Ghidra/JVM static analysis only; FPT itself is never executed."""
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent
tool=root.parent/'analysis/bios-reader-ultra-20261025/Intel-FPT-19.0.5.1948/FPTW64.exe'
assert hashlib.sha256(tool.read_bytes()).hexdigest()=='88491119862830235854cd29b9857de21870ac4dc16f74544b1a665f64d62807'
trusted=root.parent.parent/'mechrevo/.tools';ghidra=trusted/'Ghidra_12.1.4/ghidra_12.1.4_PUBLIC';java=trusted/'Temurin21/jdk-21.0.12.1+1/bin/java.exe'
work=root/'build/fpt19-static-20261025/ghidra';work.mkdir(parents=True,exist_ok=True)
followup=len(sys.argv)>1
values=sys.argv[1] if followup else (out/'selected-functions.txt').read_text().strip()
args=[str(java),'-Djava.awt.headless=true','-Djava.system.class.loader=ghidra.GhidraClassLoader','-Duser.home='+str(work),'-Dapplication.settingsdir='+str(work/'settings'),'-Dapplication.cachedir='+str(work/'cache'),'-Dapplication.tempdir='+str(work/'temp'),'-Xmx3G','-cp',str(ghidra/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(work),'Fpt19SavedFile']
args+=['-process','FPTW64.exe','-noanalysis','-readOnly'] if followup else ['-import',str(tool),'-overwrite','-analysisTimeoutPerFile','180','-max-cpu','2']
args+=['-scriptPath',str(root/'research/intel-gm6-full-spi-20261025'),'-postScript','ExportFpt.java',str(out/'decompiled'),values]
if followup:args+=['targets_only']
with (out/('ghidra-followup.txt' if followup else 'ghidra-console.txt')).open('w',encoding='utf-8') as log:
    result=subprocess.run(args,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=420)
print(json.dumps(dict(exit=result.returncode,hardware_executed=False)))
