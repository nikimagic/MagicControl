from pathlib import Path
from datetime import datetime,timezone,timedelta
import hashlib,json
root=Path.cwd();src=root/'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG';legacy=src/'legacy-battery/session-19090733014300';stamp=datetime.now(timezone(timedelta(hours=3))).strftime('%H%M%S');out=root/'MagicControl/windows/логи батареи'/('Wujie-2026-10-21-разряд-вечер-'+stamp);out.mkdir(parents=True,exist_ok=False)
files=[(src/(f'{stem}-{slot}.jsonl'),f'{stem}-{slot}.jsonl')for slot in(1,2)for stem in('MagicControl','battery')]
files +=[(p,'legacy-'+p.name)for p in legacy.glob('battery.jsonl*')]
files +=[(p,'legacy-'+p.name)for p in legacy.glob('MagicControl-*.jsonl')]
records=[]
for p,name in files:
 if not p.exists():continue
 b=p.read_bytes();end=b.rfind(b'\n')+1;complete=b[:end];dest=out/name
 with dest.open('xb')as f:f.write(complete)
 records.append({'source':str(p),'copy':str(dest),'captured_bytes':len(b),'complete_bytes':len(complete),'partial_tail_omitted':len(b)-len(complete),'sha256':hashlib.sha256(complete).hexdigest()})
manifest={'snapshot_utc':datetime.now(timezone.utc).isoformat(),'scope':'Read-only byte copies; each file captured once; only an incomplete final line omitted. Active originals unchanged. Not a single atomic cross-file snapshot.','files':records}
(out/'snapshot-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');report=root/'research/battery-discharge-20261021-evening';(report/'snapshot.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'archive':str(out),'files':len(records),'bytes':sum(r['complete_bytes']for r in records)},ensure_ascii=False))
