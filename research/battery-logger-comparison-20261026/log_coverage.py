"""Compare saved journal field definitions, never query a running application."""
from pathlib import Path
import json,re,zipfile,hashlib,collections
OUT=Path(__file__).resolve().parent;ROOT=OUT.parents[1]
def inspect(raw,source):
 records=[];errors=[]
 for n,line in enumerate(raw.decode('utf-8-sig').splitlines(),1):
  if not line.strip():continue
  try:records.append(json.loads(line))
  except json.JSONDecodeError as e:errors.append(dict(line=n,error=str(e)))
 messages=[r['message']if isinstance(r.get('message'),str)else''for r in records]
 fields=sorted({int(x,16)for s in messages for x in re.findall(r'addr=0x([0-9A-Fa-f]+)',s)})
 result=dict(source=source,captured_bytes=len(raw),captured_sha256=hashlib.sha256(raw).hexdigest(),records=len(records),channels=dict(collections.Counter(r.get('channel')for r in records)),versions=dict(collections.Counter(r.get('app_version')for r in records)),first_utc=records[0].get('utc')if records else None,last_utc=records[-1].get('utc')if records else None,explicit_definition_addresses=[f'{x:03X}'for x in fields],current_calc_messages=sum('CURRENT_CALC' in s for s in messages),parse_errors=errors,scope='One captured byte range; live file may grow later. Definition addresses are not all repeated values or every hardware capability.')
 return result
results=[]
folder=ROOT/'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG'
for p in sorted(folder.glob('battery-*.jsonl')):results.append(inspect(p.read_bytes(),str(p)))
archive=ROOT/'Users_logs/logs_GM6IR0C (18).zip'
with zipfile.ZipFile(archive)as z:
 for i in z.infolist():
  if re.fullmatch(r'logs_GM6IR0C/battery-[123]\.jsonl',i.filename):results.append(inspect(z.read(i),str(archive)+'!'+i.filename))
(OUT/'saved-log-coverage.json').write_text(json.dumps(dict(hardware_executed=False,records=results),ensure_ascii=False,indent=2),encoding='utf8')
for r in results:print(Path(r['source']).name,r['records'],r['versions'],'definition fields',len(r['explicit_definition_addresses']),'current_calc',r['current_calc_messages'],'JSON errors',len(r['parse_errors']))
