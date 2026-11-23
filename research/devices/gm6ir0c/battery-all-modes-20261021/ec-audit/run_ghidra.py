"""Fixed-bank EC static parser. Never executes firmware or opens hardware."""
from pathlib import Path
import argparse,subprocess,hashlib,uuid,re
ROOT=Path(__file__).resolve().parents[5]
p=argparse.ArgumentParser();p.add_argument('bank',type=int,choices=(0,1,2));p.add_argument('entries');p.add_argument('output');p.add_argument('--image',default='EC-MRO09-candidate.bin');p.add_argument('--suffix',default='');a=p.parse_args()
source=ROOT/'research/gm6-battery-firmware-20261021'
input_image=(source/a.image).resolve()
if not input_image.is_relative_to(source.resolve()) or not input_image.is_file():raise ValueError('Image must be inside the preserved GM6 evidence directory')
if not re.fullmatch(r'[A-Za-z0-9_-]*',a.suffix):raise ValueError('Invalid suffix')
data=input_image.read_bytes()
if hashlib.sha256(data).hexdigest() not in ('5059e6cb58addec21ad7fd5693f4c082230fbd8c7692fb07799c90adabae5c4c','5659c8217e9d93d3fd6fe278756872d7c5adbd15a0687d35705e2df4bb5c05dd'):raise ValueError('Unexpected preserved EC image SHA')
out_root=(ROOT/'out').resolve()
w=out_root/'gm6-battery-firmware-20261021'/('ghidra-'+uuid.uuid4().hex)
output_arg=Path(a.output)
output=(output_arg.resolve() if output_arg.is_absolute() else (w/'exports'/output_arg).resolve())
if not output.is_relative_to(out_root) or output.exists():raise ValueError('Output must be a new path under out')
if not output_arg.is_absolute() and not output.is_relative_to((w/'exports').resolve()):raise ValueError('Relative output escaped run directory')
w.mkdir(parents=True,exist_ok=False)
output.parent.mkdir(parents=True,exist_ok=True)
image=w/f'ec{a.suffix}-bank{a.bank}.bin';image.write_bytes(data[:0x8000]+data[a.bank*0x8000+0x8000:a.bank*0x8000+0x10000])
g=Path(r'C:\Users\admin\Documents\mechrevo\.tools\Ghidra_12.1.4\ghidra_12.1.4_PUBLIC')
java=Path(r'C:\Users\admin\Documents\mechrevo\.tools\Temurin21\jdk-21.0.12.1+1\bin\java.exe')
settings=w/f'ghidra-bank{a.bank}{a.suffix}-settings'
projects=w/'ghidra-projects';projects.mkdir(exist_ok=True)
args=[str(java),'-Djava.awt.headless=true','-Djava.system.class.loader=ghidra.GhidraClassLoader','-Duser.home='+str(w),'-Dapplication.settingsdir='+str(settings),'-Dapplication.cachedir='+str(settings/'cache'),'-Dapplication.tempdir='+str(settings/'temp'),'-Xmx2G','-cp',str(g/'Ghidra/Framework/Utility/lib/Utility.jar'),'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(projects),f'Gm6EcBank{a.bank}{a.suffix}','-import',str(image),'-overwrite','-loader','BinaryLoader','-processor','8051:BE:16:default','-cspec','default','-noanalysis','-scriptPath',str(Path(__file__).resolve().parent),'-postScript','Gm6EcExport.java',str(output),a.entries,'-log',str(w/f'ghidra-bank{a.bank}{a.suffix}.log')]
with (w/f'ghidra-bank{a.bank}{a.suffix}-console.log').open('w',encoding='utf-8')as out:
 result=subprocess.run(args,stdout=out,stderr=subprocess.STDOUT,cwd=ROOT)
print('Ghidra static parser exit',result.returncode)
raise SystemExit(result.returncode)
