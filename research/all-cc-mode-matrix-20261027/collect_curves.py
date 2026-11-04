"""Offline original curve/profile inventory; preserve raw points, never resample."""
from pathlib import Path
import json,hashlib,collections
root=Path(__file__).resolve().parents[2]
out=Path(__file__).resolve().parent
paths=(out/'profile-paths.txt').read_text(encoding='utf-8-sig').splitlines()
curves=[];profiles=[];unknown=[]
for name in paths:
    p=Path(name)
    if not p.is_absolute(): p=root/p
    data=p.read_bytes()
    entry=dict(path=str(p),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data))
    try:v=json.loads(data.decode('utf-8-sig'))
    except (ValueError,UnicodeError) as e:
        unknown.append(dict(entry,error=str(e)));continue
    if not isinstance(v,dict):unknown.append(dict(entry,reason='not an object'));continue
    if isinstance(v.get('CPU'),list) and all(isinstance(x,dict) and 'UpT' in x and 'Duty' in x for x in v['CPU']):
        curves.append(dict(entry,raw=v));continue
    if isinstance(v.get('FAN'),dict):profiles.append(dict(entry,raw=v));continue
    unknown.append(dict(entry,keys=list(v)[:20]))
(out/'CURVES.json').write_text(json.dumps(curves,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(out/'PROFILES.json').write_text(json.dumps(profiles,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(out/'UNCLASSIFIED-JSON.json').write_text(json.dumps(unknown,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
rows=['Кривые из сохранённых пакетов/installed/user файлов. Это не измерение активной EC-кривой.',
      'Исходные индексы UpT/DownT/Duty сохранены. 255 — исходный маркер, не команда/GUI диапазон.',
      'Наличие файла в многомодельном пакете не привязывает кривую к конкретному ноутбуку.','']
defaults=[x for x in curves if Path(x['path']).name.startswith('DefaultFanTable_')]
for x in defaults:
    v=x['raw'];rows.extend([x['path'],'SHA256 '+x['sha256'],
        f"Name={v.get('Name')} Respective={v.get('FanControlRespective')} CpuDefaultMaxLevel={v.get('CpuTemp_DefaultMaxLevel')} GpuDefaultMaxLevel={v.get('GpuTemp_DefaultMaxLevel')}",
        'index | CPU Up / Down / Duty | GPU Up / Down / Duty'])
    cpu=v['CPU'];gpu=v.get('GPU') or []
    for i in range(max(len(cpu),len(gpu))):
        def show(a):
            if i>=len(a):return '—'
            q=a[i];return f"{q.get('UpT')} / {q.get('DownT')} / {q.get('Duty')}"
        rows.append(f'{i:2} | {show(cpu)} | {show(gpu)}')
    rows.append('')
(out/'FACTORY-CURVES.txt').write_text('\n'.join(rows)+'\n',encoding='utf-8')
summary=dict(input_paths=len(paths),curves=len(curves),profiles=len(profiles),
             unclassified=len(unknown),default_files=len(defaults),
             unique_curve_files=len({x['sha256'] for x in curves}),
             default_sources=[dict(path=x['path'],sha256=x['sha256'],name=x['raw'].get('Name')) for x in defaults])
(out/'CURVE-INVENTORY.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k!='default_sources'}))
for x in defaults:
    print(Path(x['path']).parent.parent.name,Path(x['path']).name,
          'CPU',[(p['UpT'],p['Duty']) for p in x['raw']['CPU'] if p['UpT']<255])
