from pathlib import Path
import json,hashlib,xml.etree.ElementTree as ET
out=Path('research/battery-discharge-20261021-evening');r=json.loads((out/'evidence.json').read_text(encoding='utf-8'));events=json.loads((out/'windows-system-events.json').read_text(encoding='utf-8-sig'));ns={'e':'http://schemas.microsoft.com/win/2004/08/events/event'}
r['system_event_summary']=[]
for x in events:
 data={n.attrib['Name']:n.text for n in ET.fromstring(x['xml']).findall('e:EventData/e:Data',ns)}
 r['system_event_summary'].append({'utc':x['utc'],'id':x['id'],'record_id':x['record_id'],'message':x['message'],'data':data})
r['system_events_sha256']=hashlib.sha256((out/'windows-system-events.json').read_bytes()).hexdigest();r['current_policy_sha256']=hashlib.sha256((out/'current-windows-battery-policy.txt').read_bytes()).hexdigest();r['limits']=['EC telemetry integral only, not independent energy-meter measurement.','Whole85->6 interval has45.9% time coverage; do not extrapolate34.27Wh to complete discharge/capacity.','Reason53 at6percent is left numeric; no unverified critical-battery mapping.','Latest minimum loggedSOC is6; no5percent or battery-triggered hibernation event was captured.','Raw paused_for_command unknown values do not imply cleared hardware flags.','Current battery policy read after observation is not a historical policy snapshot.']
(out/'evidence.json').write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
archive=Path(json.loads((out/'snapshot.json').read_text(encoding='utf-8'))['files'][0]['copy']).parent
for name in ('windows-system-events.json','current-windows-battery-policy.txt'):
 with (archive/name).open('xb')as f:f.write((out/name).read_bytes())
print('archive',archive,'system events',len(events))
