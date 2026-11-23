"""Run trusted Ghidra/JVM on the saved PE. FPT and its driver are not executed."""
from pathlib import Path
import subprocess,json,sys
ROOT=Path(__file__).resolve().parents[2];SOURCE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'research/firmware-reader-static-support'))
from runtime import new_run,clone_project,selected_functions
OUT=new_run(ROOT,'fpt')
tools=ROOT.parent.parent/'mechrevo/.tools'
ghidra=tools/'Ghidra_12.1.4/ghidra_12.1.4_PUBLIC'
java=tools/'Temurin21/jdk-21.0.12.1+1/bin/java.exe'
followup=len(sys.argv)>1
work=OUT/'project';work.mkdir()
runtime=OUT/'runtime';runtime.mkdir()
if followup:clone_project(ROOT.parent/'Decompile/Firmware_Readers/FPT_AFU_Static_20261025/ghidra',work,'FptSavedFile')
values=set(selected_functions(ROOT,'fpt-inspection',SOURCE/'selected-functions.txt').read_text().strip().split(','))|{'ceb0','221da','4e770'}
if followup:values=set(sys.argv[1].split(','))
args=[str(java),'-Djava.awt.headless=true','-Djava.system.class.loader=ghidra.GhidraClassLoader',
      '-Duser.home='+str(runtime),'-Dapplication.settingsdir='+str(runtime/'settings'),
      '-Dapplication.cachedir='+str(runtime/'cache'),'-Dapplication.tempdir='+str(runtime/'temp'),
      '-Xmx3G','-cp',str(ghidra/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra',
      'ghidra.app.util.headless.AnalyzeHeadless',str(work),'FptSavedFile']
args+=['-process','FPTW64.exe','-noanalysis','-readOnly'] if followup else ['-import',str(ROOT.parent/'BIOS Unlock/FPTW64.exe'),'-overwrite','-analysisTimeoutPerFile','180','-max-cpu','2']
args+=['-scriptPath',str(SOURCE),'-postScript','ExportFpt.java',str(OUT/'decompiled'),','.join(sorted(values))]
if followup:args+=['targets_only']
with (OUT/('ghidra-followup.txt' if followup else 'ghidra-console.txt')).open('w',encoding='utf-8') as log:
    result=subprocess.run(args,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=420)
print(json.dumps({'static_analysis_exit':result.returncode,'imported_PE_executed':False,'output':str(OUT)}))
