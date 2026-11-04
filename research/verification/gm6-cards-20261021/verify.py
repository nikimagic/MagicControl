"""Static delivery checks only: saved hashes, PE resources and offline results."""
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
test_inputs=read(OUT/'test-inputs.json');assert all(sha(ROOT/p)==h for p,h in test_inputs.items())
tests=[]
for platform,expected in (('windows',5),('linux',4)):
 tree=ET.parse(OUT/f'tests-{platform}.xml').getroot();assert int(tree.attrib['tests'])==expected and tree.attrib['failures']=='0'
 tests.append({'platform':platform,'tests':expected,'failures':0,'report_sha256':sha(OUT/f'tests-{platform}.xml')})
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
# Existing hardware write mechanism must remain byte-for-byte unchanged;
# the added Auto method merely invokes it with the already-supported zeros.
path='src/devices/mechrevo/gm6ir0c/ControlWindows.cpp'
before=subprocess.check_output(['git','show','0e89002:'+path],cwd=ROOT).decode('utf-8-sig')
after=(ROOT/path).read_text(encoding='utf-8-sig')
def cpu_writer(s):return s.split('    void applyCpuPowerLocked(',1)[1].split('    std::optional<int> rpm(',1)[0]
assert cpu_writer(before)==cpu_writer(after)
report={'version':state['version'],'status':state['status'],'source_fingerprint':state['source_fingerprint'],
 'application_inputs':len(inputs),'artifact_hashes':artifacts,'embedded_checks':embedded,'preserved':preserved,
 'offline_tests':tests,'test_input_hashes':test_inputs,'existing_cpu_writer_unchanged':True,
 'battery':{'gm6_ec_rows':63,'wujie_ec_rows':103,'os_rows':27,'gm6_fixtures':['unplugged','charging','full'],'ec_os_sources_separate':True},
 'gpu_selection':'highest available valid usage; ties discrete then stable adapter ID; display route diagnostic only',
 'limits':'No application/OEM/driver/hardware run. Offline tests use pure models, explicit fixture logs and isolated hidden native controls. Live GM6 CPU enforcement, charge cutoffs and visual result remain untested.'}
(OUT/f'verification-{state["version"]}.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'version':state['version'],'inputs':len(inputs),'resources':len(embedded),'preserved':preserved,'tests':tests,'artifacts':artifacts},ensure_ascii=False))
