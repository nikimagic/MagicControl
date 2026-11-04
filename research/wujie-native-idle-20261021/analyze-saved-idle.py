from pathlib import Path
import json,hashlib,math
p=Path('tests/test_util/logs/keyboard-native-series/MECHREVO-X15PRO_windows_20261013_085150_199Z/dump.jsonl');out=Path('research/wujie-native-idle-20261021')
contexts={};frames=[]
with p.open(encoding='utf-8-sig')as f:
 for line in f:
  row=json.loads(line)
  if row.get('event')=='observation_context':contexts[row['sample']]=row
  elif row.get('event')=='ec_dump':frames.append({'sample':row['sample'],'utc':row['utc'],'failed_bytes':row['failed_bytes'],'raw':{v['address']:v.get('raw')for v in row['bytes']}})
points=[]
for frame in frames:
 c=contexts.get(frame['sample'],{});idle=c.get('keyboard',{}).get('idle_ms')
 if c.get('ac_line_status')==0 and idle is not None and 0<=idle<60000:points.append((frame,idle/1000))
print('frames',len(frames),'DC known keyboard-idle <60s',len(points))
# Exploratory screening only: no write semantics or statistical independence inferred.
ramps=[]
window=[(f,t)for f,t in points if 1<=t<=29]
for address in range(4096):
 values=[f['raw'].get(address)for f,t in window]
 if len(values)<15 or any(v is None for v in values)or len(set(values))<5:continue
 for rate in (1,2,4,8,10):
  for sign in (1,-1):
   offsets=[(v-sign*math.floor(t*rate))%256 for v,(f,t)in zip(values,window)]
   centre=max(set(offsets),key=offsets.count)
   near=sum(min((x-centre)%256,(centre-x)%256)<=1 for x in offsets)/len(offsets)
   if near>=0.9:ramps.append({'address':f'0x{address:03X}','rate_hz':rate,'direction':sign,'circular_offset':centre,'within_one_tick_fraction':near,'distinct_values':len(set(values))})
keys=[];prev=None
for f in frames:
 c=contexts.get(f['sample'],{});k=c.get('keyboard',{});events=k.get('header_events');idle=k.get('idle_ms')
 if prev is not None and events is not None and events>prev:
  i=frames.index(f);neighbors=frames[max(0,i-2):i+3];changed=[]
  if len(neighbors)>=2:
   for a in range(4096):
    seq=[x['raw'].get(a)for x in neighbors]
    if len(set(seq))>1:changed.append({'address':f'0x{a:03X}','values':seq})
  keys.append({'sample':f['sample'],'utc':f['utc'],'header_events':events,'idle_ms':idle,'neighbor_samples':[x['sample']for x in neighbors],'changed_bytes':changed})
 prev=events
bittrans=[];prev=None
for f in frames:
 val=f['raw'].get(0x741);bit=bool(val&4)if val is not None else None
 if bit!=prev:
  c=contexts.get(f['sample'],{});bittrans.append({'sample':f['sample'],'utc':f['utc'],'741':val,'bit2':bit,'keyboard_idle_ms':c.get('keyboard',{}).get('idle_ms'),'ac':c.get('ac_line_status')})
 prev=bit
report={'source':str(p),'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'frames':len(frames),'failed_bytes':sum(f['failed_bytes']for f in frames),'dc_window_points':len(points),'ramp_points':len(window),'ramp_filter':'Known keyboard idle1..29s on DC; byte ramps at 1,2,4,8,10Hz, both directions, arbitrary byte offset, at least5 distinct values and90% samples within1tick. Exploratory only; no guarantee other encodings absent.','byte_ramp_candidates':ramps,'keyboard_event_neighborhoods':keys,'bit2_transitions':bittrans,'conclusion':'Single measured keyboard-idle cycle cannot prove causality. No matched field is a confirmed timeout setting; full EC code and private XRAM are absent.'}
(out/'saved-ram-idle-screen.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('byte ramp matches',ramps);print('bit2 transitions',bittrans);print('key neighborhoods',[(k['sample'],len(k['changed_bytes']))for k in keys])
