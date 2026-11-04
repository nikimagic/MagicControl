"""Review captured portable log bytes; no app/device execution."""
from pathlib import Path
import hashlib,json,re
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
folder=ROOT/'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG'
files=[];timings=[]
for path in sorted(folder.glob('MagicControl-*.jsonl')):
    data=path.read_bytes();selected=[];versions=set()
    allrows=[]
    for raw in data.splitlines():
        try:r=json.loads(raw)
        except ValueError:continue
        versions.add(r.get('app_version'))
        allrows.append(r)
        m=str(r.get('message',''))
        if ('settings.apply' in str(r) or 'settings.dispatch' in str(r) or
            'protection configured' in m or 'voltage mode=' in m or
            'native profile' in m or 'software_command_timing' in m):
            selected.append(r)
    files.append({'path':str(path),'bytes_captured':len(data),'sha256_captured':hashlib.sha256(data).hexdigest(),
                  'versions':sorted(x for x in versions if x),'events':selected})
    for start in allrows:
        if start.get('command')!='settings.apply' or start.get('event')!='command_requested' or start.get('control_id') not in [251,252,253]:continue
        aid=start.get('action_id');end=next((r for r in allrows if r.get('action_id')==aid and r.get('command')=='settings.apply' and r.get('event')=='command_result'),None)
        if not end:continue
        related=[r for r in allrows if r.get('action_id')==aid and start['elapsed_ms']<=r['elapsed_ms']<=end['elapsed_ms']]
        counts={}
        for r in related:counts[r['channel']]=counts.get(r['channel'],0)+1
        timings.append({'file':path.name,'version':start.get('app_version'),'utc':start['utc'],'action_id':aid,'control_id':start['control_id'],
                        'apply_ms':end.get('elapsed_us',0)/1000,'outcome':end.get('outcome'),'message':end.get('message'),
                        'log_rows_by_channel':counts})
(OUT/'EVENTS.json').write_text(json.dumps(files,ensure_ascii=False,indent=2),encoding='utf8')
(OUT/'TIMINGS.json').write_text(json.dumps(timings,ensure_ascii=False,indent=2),encoding='utf8')
for t in timings:
    if t['version']=='0.1.217':print(json.dumps(t,ensure_ascii=False))
