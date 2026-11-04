"""Saved archives/ASL only. Does not import or run product or hardware code."""
from pathlib import Path
import hashlib,json,zipfile,re,collections
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[3]
ARCHIVES=Path(r'C:\Users\admin\Downloads\Telegram Desktop')
sha=lambda b:hashlib.sha256(b).hexdigest()
result={'scope':'Saved GM6 ZIP/RAM/log/ASL only; no device I/O or product execution','archives':[],'ram':[],'raw_snapshots':[],'context_records':[],'settings':[]}
seen=set()
for path in sorted(ARCHIVES.glob('*GM6*.zip')):
 entry={'name':path.name,'bytes':path.stat().st_size,'sha256':sha(path.read_bytes()),'members':0};result['archives'].append(entry)
 with zipfile.ZipFile(path)as z:
  entry['members']=len(z.infolist())
  for info in z.infolist():
   if info.filename.endswith('gm6ir0c-device-settings.ini'):
    data=z.read(info);content=data.decode('utf-8-sig',errors='replace')
    result['settings'].append({'archive':path.name,'member':info.filename,'sha256':sha(data),'battery_lines':[line for line in content.splitlines()if any(word in line.lower()for word in ('battery','charge','protection'))]})
   if info.filename.endswith(('ec.bin','ec-after.bin')):
    data=z.read(info)
    if len(data)!=4096:continue
    word=lambda a:int.from_bytes(data[a:a+2],'little')
    result['ram'].append({'archive':path.name,'member':info.filename,'sha256':sha(data),'project':data[0x740],
     '7A6':data[0x7A6],'native_mask30':data[0x7A6]&0x30,'7B9':data[0x7B9],'7D0':data[0x7D0],
     '7C3':data[0x7C3],'770':data[0x770],'741':data[0x741],'742':data[0x742],'SOC4AB':data[0x4AB],
     'state432':word(0x432),'current_mA434':word(0x434),'voltage_mV438':word(0x438),
     'remaining_mAh436':word(0x436),'full_mAh404':word(0x404),'design_mAh402':word(0x402),
     'ISDB497':data[0x497],'cycles4A6':word(0x4A6)})
   # Root-level session files only: dump copies contain the same records.
   if not info.filename.endswith('.jsonl')or '/dumps/'in info.filename:continue
   data=z.read(info)
   for line in data.decode('utf-8-sig',errors='replace').splitlines():
    try:row=json.loads(line)
    except (ValueError,TypeError):continue
    msg=row.get('message','')
    if not isinstance(msg,str):continue
    key=(row.get('session'),row.get('seq'),row.get('channel'),msg)
    if key in seen:continue
    seen.add(key)
    context=re.search(r'context7A6=0x([0-9A-Fa-f]+)',msg)
    if context:result['context_records'].append({'archive':path.name,'member':info.filename,'utc':row.get('utc'),'seq':row.get('seq'),'7A6':int(context[1],16),'native_mask30':int(context[1],16)&0x30,'message':msg})
    if row.get('channel')=='battery_candidates'and msg.startswith('SNAPSHOT '):
     fields={int(m[0],16):int(m[1],16)for m in re.findall(r'addr=0x([0-9A-Fa-f]+) name=[^;]*? value=0x([0-9A-Fa-f]+)',msg)}
     fields.update({int(m[0],16):int(m[1],16)for m in re.findall(r'0x([0-9A-Fa-f]+)=0x([0-9A-Fa-f]+)/',msg)})
     if 0x7A6 in fields:
      result['raw_snapshots'].append({'archive':path.name,'member':info.filename,'utc':row.get('utc'),'seq':row.get('seq'),'native_mask30':fields[0x7A6]&0x30,'fields':{f'{a:03X}':v for a,v in fields.items()},'message':msg})
summary={kind:dict(collections.Counter(str(x['native_mask30'])for x in result[kind]))for kind in ('ram','raw_snapshots','context_records')}
result['summary']=summary
result['unique_ram_sha256']=len({x['sha256']for x in result['ram']})
asl=[];excerpts=[]
for path in sorted((ROOT/'build/gm6-healthy-acpi').glob('*.dsl')):
 content=path.read_text(encoding='utf-8',errors='replace');lines=content.splitlines()
 tokens={t:[i+1 for i,line in enumerate(lines)if re.search(r'\b'+re.escape(t)+r'\b',line)]for t in ('CGLM','_BMC','_BMD','_BTH','_BTP','ISDB','4c2067e3-887d-475c-9720-4af1d3ed602e')}
 asl.append({'path':str(path.relative_to(ROOT)),'sha256':sha(path.read_bytes()),'tokens':tokens})
 if tokens['CGLM']:
  for line in tokens['CGLM']:
   lo=max(0,line-6);hi=min(len(lines),line+16)
   excerpts.append({'path':str(path.relative_to(ROOT)),'sha256':sha(path.read_bytes()),'first_line':lo+1,'lines':[{'line':i+1,'text':lines[i]}for i in range(lo,hi)]})
result['asl']=asl
(OUT/'census.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(OUT/'asl-excerpts.json').write_text(json.dumps(excerpts,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'archives':len(result['archives']),'ram':len(result['ram']),'unique_ram':result['unique_ram_sha256'],'summary':summary,'asl':len(asl)},ensure_ascii=False))
