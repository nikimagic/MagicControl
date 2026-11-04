"""Inspect PE resources and saved artifacts only. Does not execute any application or driver."""
from pathlib import Path
import json,hashlib,re,struct
from datetime import datetime,timezone
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[2]
def read(name):return json.loads((OUT/name).read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def resources(path):
 data=path.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
 if data[pe:pe+4]!=b'PE\0\0':raise RuntimeError('Invalid PE')
 sections=struct.unpack_from('<H',data,pe+6)[0];opt_size=struct.unpack_from('<H',data,pe+20)[0];opt=pe+24
 magic=struct.unpack_from('<H',data,opt)[0];dd=opt+(112 if magic==0x20b else 96)
 rva,size=struct.unpack_from('<II',data,dd+16)
 section_table=opt+opt_size;table=[]
 for n in range(sections):
  at=section_table+n*40;vsize,va,rawsize,raw=struct.unpack_from('<IIII',data,at+8);table.append((va,max(vsize,rawsize),raw))
 def offset(addr):
  for va,length,raw in table:
   if va<=addr<va+length:return raw+addr-va
  raise RuntimeError('Unmapped resource RVA')
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
state=json.loads((ROOT/'build/product-build.json').read_text(encoding='utf-8-sig'))
if state['status']!='complete':raise RuntimeError('Common build incomplete')
base=read('baseline.json');inputs=state['application_inputs']
changed=[p for p,h in inputs.items() if sha(ROOT/p)!=h.lower()]
personal=[]
for p,h in read('personal-before-staging.json').items():
 file=ROOT/'MagicControl'/p;current=sha(file);modified=datetime.fromtimestamp(file.stat().st_mtime,timezone.utc)
 # This individual runtime file changed after successful staging/build completion.
 # Retain both hashes; do not overwrite the user's newer settings or relabel them unchanged.
 later=p=='windows/device-settings.ini' and modified>datetime.fromisoformat(state['updated_utc'].replace('Z','+00:00'))
 personal.append({'path':p,'unchanged':current==h.lower(),'sha256_before':h.lower(),'sha256_current':current,'modified_utc':modified.isoformat(),'external_change_after_build':current!=h.lower() and later})
protected=[{'path':p,'unchanged':sha(ROOT/p)==h.lower()}for p,h in base['protected'].items()]
archive=[{'path':p,'unchanged':sha(ROOT/p)==h.lower()}for p,h in base['archive'].items()]
sources=[{'path':p,'unchanged':sha(ROOT/p)==h.lower()}for p,h in base['runtime_sources'].items()]
artifacts=[]
for platform,names in [('windows',['MagicControl.exe','mctl.exe']),('linux',['MagicControl','mctl'])]:
 folder=ROOT/'MagicControl'/platform;build=ROOT/'build'/platform/'bin'
 if platform=='windows':build/='Release'
 if(folder/'build-version.txt').read_text(encoding='utf-8-sig').strip()!=state['version']:raise RuntimeError('Wrong portable version')
 for name in names:artifacts.append({'platform':platform,'file':name,'sha256':sha(folder/name),'matches_build':sha(folder/name)==sha(build/name)})
header=(ROOT/'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text(encoding='utf-8')
catalog=re.findall(r'\{(\d+),"([^"]+)","([^"]+)","([a-f0-9]{64})",(\d+)\}',header)
if len(catalog)!=28:raise RuntimeError('Expected 28 reviewed embedded members')
embedded=[]
for name in ['MagicControl.exe','mctl.exe']:
 found=resources(ROOT/'MagicControl/windows'/name)
 for rid,group,file,digest,length in catalog:
  origin=ROOT/'src/platform/windows/runtime'/('PawnIO'if group=='Installers'else group)/file
  payloads=[b for key,b in found.items()if len(key)==3 and key[0]==10 and key[1]==int(rid)]
  if not payloads or any(len(b)!=int(length)or hashlib.sha256(b).hexdigest()!=digest or b!=origin.read_bytes() for b in payloads):raise RuntimeError('Embedded bytes mismatch: '+name+'/'+group+'/'+file)
  embedded.append({'executable':name,'resource_id':int(rid),'group':group,'name':file,'bytes':int(length),'sha256':digest,'matches_source':True})
for name in ['drivers','PawnIO']:
 if(ROOT/'MagicControl/windows'/name).exists():raise RuntimeError('Loose runtime directory remains: '+name)
if changed or not all(x['unchanged']or x['external_change_after_build']for x in personal)or not all(x['unchanged']for x in protected+archive+sources)or not all(x['matches_build']for x in artifacts):raise RuntimeError('Source/artifact/preserved-file mismatch')
report={'version':state['version'],'status':state['status'],'source_fingerprint':state['source_fingerprint'],'inputs':len(inputs),'input_mismatches':changed,'artifacts':artifacts,'embedded_files':embedded,'original_runtime_sources':sources,'personal_files':personal,'protected_ui_files':protected,'archive_files':archive,'vertical_alignment':'Native single-line EDIT client row centered by current font metrics on frame/size/font/DPI/theme changes; background padding remains clickable', 'numeric_component':'Shared Windows creator and pair placement; consumer owns dimensions/ranges/handlers', 'fan_geometry':'Preserved original full rectangles, arrow widths and scrolling offsets', 'cycle_alignment':'Edit and arrows use the same top/bottom', 'curve_row_labels':['Temp °C','PWM %'], 'cycle_control_dip':{'height':22,'minimum_edit_width':44,'arrows':14}, 'pawnio_caption':'Installed only after SCM metadata confirms presence; no service start', 'footer_status_sources':['fans','sets','keys'], 'footer_updates':'UI event notification only; no added hardware reads or polling timers', 'sets_card_visual_order':['keyboard lighting','battery and diagnostics','CPU/APU'], 'loose_runtime_directories':False,'cache':'ProgramData/MagicControlRuntime/<catalogue SHA256>; admin/SYSTEM ACL, resource/cache hashes, reparse rejection, process-lifetime read pins','installation':'Explicit INFO action only; original pinned setup embedded; no service changes during resource extraction','scope':'Compilation and static PE/resource/hash inspection only. No tests, application, GUI, cache loader or hardware executed.'}
(OUT/('verification-'+state['version']+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'version':state['version'],'application_inputs':len(inputs),'embedded_members_per_exe':len(catalog),'pe_resource_checks':len(embedded),'original_runtime_files':len(sources),'personal_unchanged':sum(x['unchanged']for x in personal),'personal_changed_after_build':sum(x['external_change_after_build']for x in personal),'protected_ui':len(protected),'archive_preserved':len(archive),'loose_dirs':False}))
