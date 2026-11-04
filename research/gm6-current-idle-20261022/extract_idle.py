import json,pathlib,re,collections
p=pathlib.Path('research/gm6ir0c-logs7-20261022/logs_GM6IR0C/battery-2.jsonl')
rs=[json.loads(x) for x in p.read_text(encoding='utf-8-sig').splitlines()]
for r in rs:
 m=str(r.get('message',''))
 if r['utc']>='2026-10-22T17:39:15' and ('addr=0x434' in m or ('stream=OS ' in m and m.startswith(('FLAGS changed','SOC changed')))):
  print(r['utc'],m[:550])
vals=[]
for r in rs:
 m=str(r.get('message',''))
 if 'stream=EC ' in m:
  ma=re.search(r' mA=(\d+(?:\.\d+)?)',m)
  if ma:vals.append(float(ma[1]))
print('EC_CURRENT_VALUES',sorted(set(vals)),'all_multiples34',all(v%34==0 for v in vals))
