from pathlib import Path
from datetime import datetime,timezone,timedelta
import json,re,collections,statistics,csv
out=Path('research/battery-discharge-20261021-evening');manifest=json.loads((out/'snapshot.json').read_text(encoding='utf-8'));rows=[];seen=set();invalid=[]
for item in manifest['files']:
 for n,line in enumerate(Path(item['copy']).read_text(encoding='utf-8-sig').splitlines(),1):
  try:r=json.loads(line)
  except ValueError:invalid.append((item['copy'],n));continue
  key=(r.get('session'),r.get('seq'))if r.get('session')and r.get('seq')is not None else line
  if key in seen:continue
  seen.add(key);r['_file']=Path(item['copy']).name;r['_t']=datetime.fromisoformat(r['utc'].replace('Z','+00:00')).timestamp()if'utc'in r else 0;rows.append(r)
rows.sort(key=lambda r:(r['_t'],r.get('seq',0)))
def clock(t):return datetime.fromtimestamp(t,timezone(timedelta(hours=3))).strftime('%H:%M:%S')
def pairs(msg):return dict(re.findall(r'(?<!\w)([A-Za-z_][A-Za-z0-9_]*)=([^ ;]+)',msg.split(' fields=[')[0]))
soc=[];samples=[];flags=[];power=[];special=[];states={};regtrans=[]
for r in rows:
 msg=str(r.get('message',''));v=pairs(msg);t=r['_t']
 if msg.startswith('SOC ')and v.get('stream')=='EC':soc.append({'time':clock(t),'utc':r['utc'],'t':t,'percent':int(v['current']),'W':float(v['W']),'V':float(v['V']),'mA':float(v['mA']),'C':float(v['C']),'flow':v['flow'],'details':msg})
 if msg.startswith('FLAGS ')and ' stream=EC 'in msg:flags.append({'time':clock(t),'utc':r['utc'],'details':msg})
 if r.get('channel')=='battery_candidates'and all(k in v for k in('percent','W','V','mA')):
  state=states.setdefault(r.get('session',r['_file']),{})
  values=[]
  if msg.startswith('CHANGED'):
   values=[(int(a,16),int(b,16)if b!='unknown'else None)for a,b in re.findall(r'addr=0x([0-9A-F]+) name=[^;]+? current=(0x[0-9A-F]+|unknown)',msg)]
  elif'reason=initial 'in msg:
   values=[(int(a,16),int(b,16)if b!='unknown'else None)for a,b in re.findall(r'addr=0x([0-9A-F]+) name=[^;]+? value=(0x[0-9A-F]+|unknown)',msg)]
  else:values=[(int(a,16),int(b,16)if b!='unknown'else None)for a,b in re.findall(r'0x([0-9A-F]+)=(0x[0-9A-F]+|unknown)/',msg)]
  for a,value in values:
   if a in state and state[a]!=value:regtrans.append({'t':t,'time':clock(t),'address':f'0x{a:03X}','old':state[a],'new':value,'percent':int(v['percent'])})
   state[a]=value
  try:p={'t':t,'time':clock(t),'utc':r['utc'],'soc':int(v['percent']),'W':float(v['W']),'V':float(v['V']),'mA':float(v['mA']),'C':float(v['C']),'remaining_mAh':float(v['remaining_mAh'])if v.get('remaining_mAh','unknown')!='unknown'else None,'full_mAh':float(v['full_mAh'])if v.get('full_mAh','unknown')!='unknown'else None,'raw':state.copy(),'seq':r.get('seq'),'session':r.get('session')}
  except ValueError:continue
  if v.get('core_stable')=='true':samples.append(p)
 if r.get('event')=='windows_power_event':power.append({'time':clock(t),'utc':r['utc'],'t':t,'message':r.get('message'),'windows_power':r.get('windows_power'),'setting_guid':r.get('setting_guid'),'value':r.get('value')})
 if r.get('event')=='application_action' or (r.get('channel')not in('battery','battery_candidates')and any(s in msg.lower()for s in('suspend','resume','system_sleep_clock','exit:','build='))):special.append({'time':clock(t),'utc':r['utc'],'channel':r.get('channel'),'event':r.get('event'),'message':msg,'action':r.get('action'),'detail':r.get('detail')})
lateststart=datetime.fromisoformat('2026-10-21T15:36:06+00:00').timestamp();latest=[r for r in rows if r['_t']>=lateststart]
min_soc=min(p['soc']for p in samples if p['t']>=lateststart);first_min=next(p for p in samples if p['t']>=lateststart and p['soc']==min_soc and p['W']<0)
# Identify last discharge run from maximum positive-to-negative phase in this saved day.
last_discharge=[p for p in samples if p['W']<0 and p['t']<=first_min['t']];peak=max(p['soc']for p in last_discharge);start=next(p for p in last_discharge if p['soc']==peak)
def stats(a,b):
 s=[p for p in samples if a<=p['t']<=b and p['W']<0];dt=b-a;wh=mah=covered=0;gaps=[]
 for x,y in zip(s,s[1:]):
  d=y['t']-x['t']
  if d>15 or x['session']!=y['session']:gaps.append({'from':x['time'],'to':y['time'],'seconds':round(d,3)});continue
  if d<=0:continue
  covered+=d;wh+=-(x['W']+y['W'])/2*d/3600;mah+=(x['mA']+y['mA'])/2*d/3600
 return {'wall_minutes':round(dt/60,3),'covered_minutes':round(covered/60,3),'coverage_percent':round(100*covered/dt,3)if dt else 0,'covered_discharge_Wh':round(wh,4),'covered_discharge_mAh':round(mah,3),'mean_W_on_covered_time':round(wh*3600/covered,3)if covered else None,'gaps':gaps,'samples':len(s),'soc_start':s[0]['soc']if s else None,'soc_end':s[-1]['soc']if s else None,'V_min':min(p['V']for p in s)if s else None,'V_max':max(p['V']for p in s)if s else None,'C_min':min(p['C']for p in s)if s else None,'C_max':max(p['C']for p in s)if s else None}
selected=['0x432','0x490','0x497','0x742','0x7B9','0x7D0','0x7C3','0x7CC','0x7CD','0x7CE','0x520','0x522','0x7A6','0x7A5','0x7C8','0x741']
first_latest=next(p for p in samples if p['t']>=lateststart);last_before_charge=max((p for p in samples if p['t']>=lateststart and p['W']<0),key=lambda p:p['t']);first_charge=next(p for p in samples if p['t']>last_before_charge['t'] and p['W']>0)
latestgaps=[{'from':clock(x['_t']),'to':clock(y['_t']),'seconds':round(y['_t']-x['_t'],3)}for x,y in zip(latest,latest[1:])if y['_t']-x['_t']>15]
r={'snapshot':manifest['snapshot_utc'],'rows_deduplicated':len(rows),'invalid_lines':invalid,'peak_discharge_start':{k:v for k,v in start.items()if k!='raw'},'first_minimum':{k:v for k,v in first_min.items()if k!='raw'},'last_discharge':{k:v for k,v in last_before_charge.items()if k!='raw'},'first_charge':{k:v for k,v in first_charge.items()if k!='raw'},'full_saved_discharge_stats':stats(start['t'],last_before_charge['t']),'latest_discharge_stats':stats(first_latest['t'],last_before_charge['t']),'latest_log_gaps_over15s':latestgaps,'latest_power_events':[p for p in power if p['t']>=lateststart],'latest_flag_transitions':[p for p in regtrans if p['t']>=lateststart and p['address']in selected],'latest_flag_values':{a:sorted({p['raw'][int(a,16)]for p in samples if p['t']>=lateststart and int(a,16)in p['raw']},key=lambda x:-1 if x is None else x)for a in selected},'soc_timeline':soc,'ec_flow_events':flags,'special_events':special}
latest_samples=[p for p in samples if p['t']>=lateststart]
telemetry={0x402,0x404,0x436,0x434,0x438,0x4A2,0x4A4,0x4A6,0x4AB,0x51C,0x44C}
r['latest_other_candidate_transitions']=[p for p in regtrans if p['t']>=lateststart and int(p['address'],16)not in telemetry]
r['remaining_capacity_quantization']={'samples':len(latest_samples),'match_soc_times_full':sum(p['remaining_mAh']==p['soc']*p['full_mAh']/100 for p in latest_samples if p['remaining_mAh']is not None and p['full_mAh']is not None),'interpretation':'Arithmetic match in observed records; capacities are not independent validation of SOC, and this does not identify which firmware field derives from which.'}
latest_soc=[p for p in soc if p['t']>=lateststart and p['flow']=='discharging']
r['low_soc_step_stats']=[]
for a,b in zip(latest_soc,latest_soc[1:]):
 if a['percent']<=15:r['low_soc_step_stats'].append({'from_soc':a['percent'],'to_soc':b['percent'],'from_time':a['time'],'to_time':b['time'],**stats(a['t'],b['t'])})
(out/'evidence.json').write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
with(out/'soc.csv').open('w',encoding='utf-8-sig',newline='')as f:
 w=csv.DictWriter(f,fieldnames=['time','utc','percent','flow','W','V','mA','C']);w.writeheader();w.writerows({k:p[k]for k in w.fieldnames}for p in soc)
print(json.dumps({k:r[k]for k in('first_minimum','last_discharge','first_charge','full_saved_discharge_stats','latest_discharge_stats','latest_log_gaps_over15s','latest_power_events','latest_flag_values')},ensure_ascii=False,indent=2))

