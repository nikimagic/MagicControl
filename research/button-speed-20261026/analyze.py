"""Capture and time existing portable log records; no device/application calls."""
from pathlib import Path
import hashlib,json,re,statistics
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
folder=ROOT/'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG'
def category(i):
    if 100<=i<=103:return 'battery_percent'
    if 251<=i<=253:return 'battery_native'
    if 200<=i<=202:return 'ec_policy'
    if 110<=i<=114 or 180<=i<=183:return 'cpu'
    if 120<=i<=123:return 'lighting_effect'
    if 130<=i<=137:return 'lighting_colour'
    if 140<=i<=144 or i in [150,151,220]:return 'lighting_other'
    if 240<=i<=243:return 'logger'
    return 'other'
files=[];operations=[];samples=[];gaps=[];breakdown=[]
for path in sorted(folder.glob('MagicControl-*.jsonl')):
    raw=path.read_bytes();rows=[]
    for line in raw.splitlines():
        try:r=json.loads(line)
        except ValueError:continue
        if isinstance(r,dict):rows.append(r)
    files.append({'path':str(path),'bytes_captured':len(raw),'sha256_captured':hashlib.sha256(raw).hexdigest(),
                  'versions':sorted({r['app_version'] for r in rows if r.get('app_version')}),
                  'start_utc':rows[0].get('utc') if rows else None,'last_utc':rows[-1].get('utc') if rows else None})
    by_action={}
    for r in rows:by_action.setdefault(r.get('action_id'),[]).append(r)
    if files[-1]['versions']==['0.1.219']:
        for aid in [10,11,14,166,179,181,182,187,189,190]:
            related=by_action.get(aid,[])
            commands={}
            exchange_pending=None;by_address={}
            for r in related:
                if r.get('command')=='uniwill.acpi_exchange' and r.get('event')=='command_requested':exchange_pending=r
                if r.get('event')=='command_result' and 'elapsed_us' in r:
                    commands.setdefault(r.get('command'),[]).append(r['elapsed_us']/1000)
                    if r.get('command')=='uniwill.acpi_exchange' and exchange_pending is not None:
                        address=hex(exchange_pending.get('address',0));by_address.setdefault(address,[]).append(r['elapsed_us']/1000);exchange_pending=None
            milestones=[{'utc':r['utc'],'elapsed_ms':r['elapsed_ms'],'message':r['message']} for r in related
                        if r.get('channel')=='battery' and isinstance(r.get('message'),str) and any(s in r['message'] for s in ['LIMIT','policy','platform','configured'])]
            breakdown.append({'action_id':aid,'commands':{k:{'count':len(v),'sum_ms_nested':sum(v),'max_ms':max(v)} for k,v in commands.items()},
                              'exchange_by_address':{k:{'count':len(v),'sum_ms':sum(v),'max_ms':max(v)} for k,v in sorted(by_address.items(),key=lambda kv:sum(kv[1]),reverse=True)},
                              'battery_messages':milestones})
            for a,b in zip(related,related[1:]):
                delta=b['elapsed_ms']-a['elapsed_ms']
                if delta>=45:
                    clean=lambda r:{k:v for k,v in r.items() if k in ['utc','elapsed_ms','seq','channel','event','command','message','address','value','input_hex','ioctl','argument','result']}
                    gaps.append({'action_id':aid,'gap_ms':delta,'before':clean(a),'after':clean(b)})
    for r in rows:
        if r.get('command')!='settings.apply' or r.get('event')!='command_requested':continue
        aid=r.get('action_id');i=r.get('control_id',-1);related=by_action.get(aid,[])
        end=next((x for x in related if x.get('command')=='settings.apply' and x.get('event')=='command_result' and x['seq']>r['seq']),None)
        if not end:continue
        dispatch=[x for x in related if x.get('command')=='settings.dispatch' and x.get('event')=='command_requested' and x['seq']<r['seq']]
        first=dispatch[0] if dispatch else r
        stages=[x.get('message') for x in related if isinstance(x.get('message'),str) and 'software_command_timing' in x['message']]
        confirmation=next((x['message'] for x in related if isinstance(x.get('message'),str) and x['message'].startswith('LIMIT confirmed:')),'')
        policy=re.search(r'platform=(\d+) rom=0x([0-9A-Fa-f]+)',confirmation)
        counts={}
        for x in related:
            if r['seq']<=x['seq']<=end['seq']:counts[x['channel']]=counts.get(x['channel'],0)+1
        operations.append({'version':r.get('app_version'),'file':path.name,'utc':r['utc'],'action_id':aid,'control_id':i,
                           'category':category(i),'worker_ms':end.get('elapsed_us',0)/1000,
                           'dispatch_to_result_ms':end['elapsed_ms']-first['elapsed_ms'],
                           'queue_before_worker_ms':r['elapsed_ms']-first['elapsed_ms'],
                           'outcome':end.get('outcome'),'message':end.get('message'),'stages':stages,'rows_by_channel':counts,
                           'confirmed_policy':policy.group(1)+'/'+policy.group(2).upper() if policy else None})
    if files[-1]['versions']==['0.1.219']:
        for r in rows:
            if r.get('channel')=='action' and r.get('event') not in ['command_requested','command_result'] and r.get('action_id') is not None:
                samples.append({k:v for k,v in r.items() if k not in ['controls_before','observed_state','cached_before']})
                if len(samples)>=4:break
summary=[]
for version,cat in sorted({(r['version'],r['category']) for r in operations}):
    selected=[r for r in operations if r['version']==version and r['category']==cat]
    values=[r['worker_ms'] for r in selected];total=[r['dispatch_to_result_ms'] for r in selected]
    summary.append({'version':version,'category':cat,'count':len(values),'worker_min_ms':min(values),'worker_median_ms':statistics.median(values),
                    'worker_max_ms':max(values),'dispatch_max_ms':max(total),'queue_max_ms':max(r['queue_before_worker_ms'] for r in selected),
                    'failures':sum(r['outcome'] not in ['completed_with_observation','readback_confirmed'] for r in selected)})
evidence={'scope':'Captured log bytes only. Software processing and dispatch timing; not physical response/UI paint timing.',
          'files':files,'summary':summary,'operations':operations,'action_shape_samples':samples}
(OUT/'TIMINGS.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf8')
(OUT/'GAPS.json').write_text(json.dumps(gaps,ensure_ascii=False,indent=2),encoding='utf8')
(OUT/'BREAKDOWN.json').write_text(json.dumps(breakdown,ensure_ascii=False,indent=2),encoding='utf8')
for f in files:print('FILE',Path(f['path']).name,f['versions'],f['bytes_captured'],f['last_utc'])
for s in summary:print(json.dumps(s,ensure_ascii=False))
print('BREAKDOWN')
for item in breakdown:
    if item['action_id'] in [10,11,179,182,190]:print(item['action_id'],json.dumps(dict(list(item['exchange_by_address'].items())[:3]),ensure_ascii=False))
for policy in sorted({r['confirmed_policy'] for r in operations if r['version']=='0.1.219' and r['category']=='battery_percent' and r['confirmed_policy']}):
    v=[r['worker_ms'] for r in operations if r['version']=='0.1.219' and r['category']=='battery_percent' and r['confirmed_policy']==policy]
    print('PERCENT POLICY',policy,'n',len(v),'min',min(v),'median',statistics.median(v),'max',max(v))
