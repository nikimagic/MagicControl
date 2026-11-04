from pathlib import Path
import subprocess,json
root=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent
tools=root.parent.parent/'mechrevo/.tools';ghidra=tools/'Ghidra_12.1.4/ghidra_12.1.4_PUBLIC'
java=tools/'Temurin21/jdk-21.0.12.1+1/bin/java.exe'
work=root/'build/honor-manual-static-20261101';work.mkdir(exist_ok=True)
for module in ['CbsBaseDxeRN','SmuV12Dxe']:
 args=[str(java),'-Djava.awt.headless=true','-Djava.system.class.loader=ghidra.GhidraClassLoader',
       '-Duser.home='+str(work),'-Dapplication.settingsdir='+str(work/'settings'),
       '-Dapplication.cachedir='+str(work/'cache'),'-Dapplication.tempdir='+str(work/'temp'),
       '-Xmx2G','-cp',str(ghidra/'Ghidra/Framework/Utility/lib/Utility.jar'),
       'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(work),'HonorManual_'+module,
       '-import',str(out/module/'body.bin'),'-overwrite','-analysisTimeoutPerFile','90','-max-cpu','2',
       '-scriptPath',str(out),'-postScript','ExportManualCandidates.java',str(out/(module+'-decompiled'))]
 with (out/(module+'-ghidra.log')).open('w',encoding='utf-8') as log:
  p=subprocess.run(args,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=300)
 print(json.dumps({'module':module,'analysis_exit':p.returncode,'vendor_code_executed':False}),flush=True)
 if p.returncode:raise SystemExit(p.returncode)
