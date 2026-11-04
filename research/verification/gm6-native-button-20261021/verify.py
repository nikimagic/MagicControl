"""Static delivery checks only: saved hashes and PE resources; never execute application or tests."""
from pathlib import Path
import json,hashlib,re,struct,xml.etree.ElementTree as ET,subprocess
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[2]
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def resources(path):
 data=path.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
 assert data[pe:pe+4]==b'PE\0\0'
 sections=struct.unpack_from('<H',data,pe+6)[0];opt_size=struct.unpack_from('<H',data,pe+20)[0];opt=pe+24
 dd=opt+(112 if struct.unpack_from('<H',data,opt)[0]==0x20b else 96)
 rva,size=struct.unpack_from('<II',data,dd+16);table=[]
 for n in range(sections):
  at=opt+opt_size+n*40;vsize,va,rawsize,raw=struct.unpack_from('<IIII',data,at+8);table.append((va,max(vsize,rawsize),raw))
 def offset(addr):
  for va,length,raw in table:
   if va<=addr<va+length:return raw+addr-va
  raise RuntimeError('Unmapped RVA')
 base=offset(rva);result={}
 def walk(relative,keys=()):
  at=base+relative;named,ids=struct.unpack_from('<HH',data,at+12)
  for n in range(named+ids):
   key,next_at=struct.unpack_from('<II',data,at+16+n*8)
   if key&0x80000000:continue
   if next_at&0x80000000:walk(next_at&0x7fffffff,keys+(key,))
   else:
    value_rva,length=struct.unpack_from('<II',data,base+next_at);start=offset(value_rva);result[keys+(key,)]=data[start:start+length]
 walk(0);return result
state=read(ROOT/'build/product-build.json');assert state['status']=='complete'
inputs=state['application_inputs'];assert all(sha(ROOT/p)==h for p,h in inputs.items())
baseline=read(OUT/'baseline.json');preserved={}
for kind in ('archive','runtime_sources','metadata','protected'):
 for p,h in baseline[kind].items():assert sha(ROOT/p)==h,p
 preserved[kind]=len(baseline[kind])
for p,h in read(OUT/'personal-before-staging.json').items():assert sha(ROOT/'MagicControl'/p)==h,p
preserved['personal_runtime_files']=len(baseline['personal'])
artifacts=[]
for platform,names in [('windows',['MagicControl.exe','mctl.exe']),('linux',['MagicControl','mctl'])]:
 folder=ROOT/'MagicControl'/platform;build=ROOT/'build'/platform/'bin'
 if platform=='windows':build/='Release'
 assert(folder/'build-version.txt').read_text(encoding='utf-8-sig').strip()==state['version']
 for name in names:
  assert sha(folder/name)==sha(build/name)
  artifacts.append({'platform':platform,'file':name,'sha256':sha(folder/name),'matches_build':True})
header=(ROOT/'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text(encoding='utf-8')
catalog=re.findall(r'\{(\d+),"([^"]+)","([^"]+)","([a-f0-9]{64})",(\d+)\}',header);assert len(catalog)==28
embedded=[]
for name in ('MagicControl.exe','mctl.exe'):
 found=resources(ROOT/'MagicControl/windows'/name)
 for rid,group,file,digest,length in catalog:
  source=ROOT/'src/platform/windows/runtime'/('PawnIO'if group=='Installers'else group)/file
  payloads=[b for key,b in found.items()if len(key)==3 and key[0]==10 and key[1]==int(rid)]
  assert payloads and all(len(b)==int(length)and hashlib.sha256(b).hexdigest()==digest and b==source.read_bytes()for b in payloads)
  embedded.append({'executable':name,'member':group+'/'+file,'sha256':digest})
assert not any((ROOT/'MagicControl/windows'/p).exists()for p in ('drivers','PawnIO'))
path='src/devices/mechrevo/gm6ir0c/ControlWindows.cpp'
after=(ROOT/path).read_text(encoding='utf-8-sig')
def battery_body(s):return s.split('EcBatteryReading readGm6EcBatteryOnly()',1)[1].split('Gm6Controller::Gm6Controller()',1)[0]
assert hashlib.sha256(battery_body(after).encode()).hexdigest()==read(OUT/'battery-body-sha.json')['sha256']
report={'version':state['version'],'status':state['status'],'source_fingerprint':state['source_fingerprint'],
 'application_inputs':len(inputs),'artifact_hashes':artifacts,'embedded_checks':embedded,'preserved':preserved,
 'battery_reader_body_unchanged':True,'tests_run':False,
 'scope':'GM6 native Office/Gaming/Turbo/Custom worker and indication flags; CPU-only Economy10/15/20 and Auto0/0/0; shared English power labels.',
 'limitations':'No application/OEM/driver/hardware execution. Native LED colours, event delivery and physical CPU/FAN effects require owner verification. GPU/TCC/voltage/NVRAM/battery setters were not copied.'}
(OUT/f'verification-{state["version"]}.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'version':state['version'],'inputs':len(inputs),'resources':len(embedded),'preserved':preserved,'artifacts':artifacts},ensure_ascii=False))
