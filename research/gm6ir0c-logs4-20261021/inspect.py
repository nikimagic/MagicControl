from pathlib import Path
import json,collections,re,uuid
root=Path('research/gm6ir0c-logs4-20261021/logs_GM6IR0C');out=Path('out/gm6ir0c-logs4-20261021')/('inspect-'+uuid.uuid4().hex);out.mkdir(parents=True,exist_ok=False);report={'sessions':[],'dumps':[]}
for p in root.glob('MagicControl-*.jsonl'):
 rows=[json.loads(x)for x in p.read_text(encoding='utf-8-sig').splitlines()];counts=collections.Counter(r.get('channel')for r in rows);gm=collections.Counter(str(r.get('message',''))for r in rows if r.get('channel')=='gm6ir0c');errors=collections.Counter(str(r.get('message',''))for r in rows if any(s in str(r.get('message','')).lower()for s in('error','failed','ошиб','unavailable','not confirmed')))
 report['sessions'].append({'file':p.name,'rows':len(rows),'first':rows[0].get('utc'),'last':rows[-1].get('utc'),'channels':dict(counts),'gm6_messages':{k:v for k,v in gm.items()if not k.startswith('mode=')},'error_message_examples_top20':dict(errors.most_common(20))})
 print('SESSION',p.name,len(rows),rows[0].get('utc'),rows[-1].get('utc'),counts)
 for msg,n in gm.items():
  if not msg.startswith('mode='):print('GM6',n,msg[:900])
for p in root.rglob('ec.bin'):
 folder=p.parent;b=p.read_bytes();w=lambda a:b[a]|b[a+1]<<8;regs={f'{a:03X}':w(a)for a in(0x400,0x402,0x404,0x408,0x432,0x434,0x436,0x438,0x43A,0x4A6)};regs.update({f'{a:03X}':b[a]for a in(0x43C,0x490,0x494,0x497,0x49F,0x7A6,0x7B9,0x7D0,0x7CC,0x7CD,0x7CE,0x727,0x751,0x783,0x784,0x785)});row={'folder':str(folder),'registers':regs,'PL_defaults':[[b[a]for a in add]for add in((0x734,0x735,0x736),(0x730,0x731,0x732),(0x7A7,0x7A8,0x7A9))]}
 for name in('battery.json','nvidia.json'):
  if(folder/name).exists():
   data=json.loads((folder/name).read_text(encoding='utf-8-sig'))
   if name=='nvidia.json':data=[{k:x.get(k)for k in('temperature_c','gpu_usage_percent','power_mw','limit_mw','enforced_mw','default_mw','minimum_mw','maximum_mw','power_control_reported','status')}for x in data.get('devices',[])]
   else:data={k:data.get(k)for k in('online','percent','watts','volts','milliamps','health_percent','cycles','flow')}
   row[name]=data
 report['dumps'].append(row);print('DUMP',json.dumps(row,ensure_ascii=False))
(out/'initial-archive-summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
