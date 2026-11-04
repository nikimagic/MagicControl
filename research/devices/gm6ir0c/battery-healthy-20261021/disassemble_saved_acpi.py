"""Disassemble saved AML only. Never acquires current-machine tables or evaluates AML."""
from pathlib import Path
import hashlib,json,subprocess,re,collections
ROOT=Path(__file__).resolve().parents[4]
OUT=Path(__file__).resolve().parent
capture_source=ROOT/'build/gm6ir0c-logs4-20261021/logs_GM6IR0C/dumps/dump-20261021-175302-0/firmware-before/acpi'
source=capture_source if capture_source.exists()else OUT/'saved-acpi'
work=ROOT/'build/gm6-healthy-acpi';work.mkdir(exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
tables={};observed=[]
for p in source.glob('*.aml'):
 b=p.read_bytes();sig=b[:4].decode('ascii',errors='replace');h=sha(b)
 observed.append({'source':p.name,'signature':sig,'bytes':len(b),'sha256':h})
 if sig not in ('DSDT','SSDT'):continue
 assert len(b)>=36 and int.from_bytes(b[4:8],'little')==len(b)and sum(b)%256==0
 if h not in tables:
  target=work/f'{sig}-{h[:12]}.aml';target.write_bytes(b)
  tables[h]={'source':p.name,'target':target.name,'signature':sig,'bytes':len(b),'sha256':h,'checksum_valid':True}
def wsl(p):return '/mnt/c/'+str(p.resolve())[3:].replace('\\','/')
iasl=ROOT/'build/acpica-gm6-healthy/package/usr/bin/iasl'
args=['wsl.exe','-d','Ubuntu-26.04','--exec',wsl(iasl),'-da','-dl',*[wsl(work/t['target'])for t in tables.values()]]
run=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(work/'iasl-output.txt').write_text(run.stdout,encoding='utf-8')
individual=[]
if run.returncode:
 # API/registry captures may contain alternative tables defining the same names.
 # Decode separately rather than presenting a failed joint namespace as absence.
 code="""import subprocess,sys,json
results=[]
for p in sys.argv[2:]:
 r=subprocess.run([sys.argv[1],'-d','-dl',p],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace')
 results.append({'file':p,'return_code':r.returncode,'output':r.stdout})
print(json.dumps(results))
"""
 separate=subprocess.run(['wsl.exe','-d','Ubuntu-26.04','--exec','python3','-c',code,wsl(iasl),*[wsl(work/t['target'])for t in tables.values()]],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
 if separate.returncode:raise RuntimeError(separate.stdout[-1500:])
 individual=json.loads(separate.stdout)
 (work/'iasl-individual.json').write_text(json.dumps(individual,indent=2),encoding='utf-8')
search={};patterns={'standard_charge_control':r'\b(?:_BMC|_BMD|_BTH)\b','configured_limit':r'\bCGLM\b','native_policy_named':r'BatteryLimitation|HealthProtection|HEALTHYMODE','policy_offset':r'Offset\s*\(0x0?7A6\)','ec_write_transport':r'Method\s*\(ECRW','memory_write_transport':r'Method\s*\(MMRW'}
for key,pattern in patterns.items():
 hits=[]
 for p in work.glob('*.dsl'):
  for n,line in enumerate(p.read_text(encoding='utf-8-sig',errors='replace').splitlines(),1):
   if re.search(pattern,line,re.I):hits.append({'file':p.name,'line':n,'text':line.strip()})
 search[key]=hits
report={'scope':'Saved GM6IR0C AML files only; iasl -da -dl, no AML execution or current-machine table acquisition.',
 'source_folder':str(source.relative_to(ROOT)).replace('\\','/'),'observed':observed,'unique_aml':list(tables.values()),
 'iasl_sha256':sha(iasl.read_bytes()),'joint_iasl_return_code':run.returncode,'individual_results':[{'file':r['file'].rsplit('/',1)[-1],'return_code':r['return_code']}for r in individual],
 'generated_dsl':sorted(p.name for p in work.glob('*.dsl')),'searches':search,
 'limit':'Negative matches apply only to these saved tables. They cannot exclude EC firmware/SMM algorithms or tables absent from this capture.'}
(OUT/'saved-acpi-analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'saved_acpi_files':len(observed),'unique_aml':len(tables),'generated_dsl':len(report['generated_dsl']),'joint_iasl_exit':run.returncode,'individual_exit_codes':[r['return_code']for r in individual],'matches':{k:len(v)for k,v in search.items()}},ensure_ascii=False))
