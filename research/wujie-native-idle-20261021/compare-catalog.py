from pathlib import Path
import json
p=Path('research/wujie-native-idle-20261021/sources/official-catalog-first.body');fresh=json.loads(p.read_text());old=json.loads(Path('research/analysis-20261020/wujie-ec-firmware/sources/driver-pages-500/page-1.json').read_text());a=fresh['result'];b=old['result'];aa={x['id']:x for x in a['objects']};bb={x['id']:x for x in b['objects']};print('totals',a['total'],b['total'],'same_objects',aa==bb,'newids',sorted(set(aa)-set(bb)))
for o in a['objects']:
 if any(s in o.get('title','').lower()for s in ('bios','控制中心','固件')):print(json.dumps({k:o.get(k)for k in ('id','title','source_url','published_at','driver_category_names')},ensure_ascii=False))
