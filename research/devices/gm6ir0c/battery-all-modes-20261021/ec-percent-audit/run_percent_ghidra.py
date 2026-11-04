"""Fixed-bank EC static parser. Never executes firmware or opens hardware."""
from pathlib import Path
import argparse,subprocess,hashlib
ROOT=Path(__file__).resolve().parents[5]
p=argparse.ArgumentParser();p.add_argument('bank',type=int,choices=(0,1,2));p.add_argument('entries');p.add_argument('output');p.add_argument('--image',default='EC-MRO09-candidate.bin');p.add_argument('--suffix',default='');p.add_argument('--raw',action='store_true');a=p.parse_args()
w=ROOT/'build/gm6-battery-firmware-20261021';w.mkdir(exist_ok=True)
data=(w/a.image).read_bytes()
assert hashlib.sha256(data).hexdigest()in ('5059e6cb58addec21ad7fd5693f4c082230fbd8c7692fb07799c90adabae5c4c','5659c8217e9d93d3fd6fe278756872d7c5adbd15a0687d35705e2df4bb5c05dd')
image=w/f'ec-percent{a.suffix}-bank{a.bank}.bin';image.write_bytes(data[:0x8000]+data[a.bank*0x8000+0x8000:a.bank*0x8000+0x10000])
g=Path(r'C:\Users\admin\Documents\mechrevo\.tools\Ghidra_12.1.4\ghidra_12.1.4_PUBLIC')
java=Path(r'C:\Users\admin\Documents\mechrevo\.tools\Temurin21\jdk-21.0.12.1+1\bin\java.exe')
settings=w/f'ghidra-percent-bank{a.bank}{a.suffix}-settings';output=Path(a.output).resolve();output.parent.mkdir(parents=True,exist_ok=True)
projects=w/'ghidra-projects';projects.mkdir(exist_ok=True)
args=[str(java),'-Djava.awt.headless=true','-Djava.system.class.loader=ghidra.GhidraClassLoader','-Duser.home='+str(w),'-Dapplication.settingsdir='+str(settings),'-Dapplication.cachedir='+str(settings/'cache'),'-Dapplication.tempdir='+str(settings/'temp'),'-Xmx2G','-cp',str(g/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(projects),f'Gm6EcPercent{a.bank}{a.suffix}','-import',str(image),'-overwrite','-loader','BinaryLoader','-processor','8051:BE:16:default','-cspec','default','-noanalysis','-scriptPath',str(Path(__file__).resolve().parent),'-postScript',('Gm6EcRawRanges.java' if a.raw else 'Gm6EcExport.java'),str(output),a.entries,'-log',str(w/f'ghidra-percent-bank{a.bank}{a.suffix}.log')]
with (w/f'ghidra-percent-bank{a.bank}{a.suffix}-console.log').open('w',encoding='utf-8')as out:
 result=subprocess.run(args,stdout=out,stderr=subprocess.STDOUT,cwd=ROOT)
print('Ghidra static parser exit',result.returncode)
raise SystemExit(result.returncode)


