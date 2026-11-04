import pathlib,json,collections,re
r=pathlib.Path('.')
out={}
for model,folder in [('GM5','research/gm5hg0a-logs4-20261022'),('L16','research/l16-2zip-20261022'),('Wujie','research/wujie-live-charge-20261022')]:
 d={'dumps':[],'idle_examples':[],'current_sample_mod34':{}}
 currents=[];idles=[]
 for f in (r/folder).rglob('ec.bin'):
  b=f.read_bytes(); v=f.with_name('ec-valid.bin');v=v.read_bytes() if v.exists() else None
  if len(b)<0x500:continue
  d['dumps'].append({'file':str(f),'regs':{hex(a):{'word':int.from_bytes(b[a:a+2],'little'),'valid':all(v[a:a+2]) if v else None} for a in [0x400,0x432,0x434,0x438,0x497]}})
 for f in (r/folder).rglob('battery-*.jsonl'):
  for line in f.read_text(encoding='utf-8-sig').splitlines():
   x=json.loads(line);m=str(x.get('message',''))
   if 'stream=EC ' not in m:continue
   ma=re.search(r' mA=([0-9.]+)',m)
   if ma:currents.append(float(ma[1]))
   if 'flow=idle' in m and len(idles)<2:idles.append({'utc':x['utc'],'text':m[:900]})
 d['idle_examples']=idles
 d['current_sample_mod34']={'n':len(currents),'unique':sorted(set(currents))[:20],'all_multiple34':all(x%34==0 for x in currents) if currents else None}
 out[model]=d
pathlib.Path('research/gm6-current-idle-20261022/comparison.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(out,ensure_ascii=False,indent=2))
