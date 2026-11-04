"""Offline links between original hardware reports and saved EC RAM snapshots."""
from pathlib import Path,PurePosixPath
import json,zipfile,hashlib,collections,re,io
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[1]
sha=lambda raw:hashlib.sha256(raw).hexdigest()
reports=[];dumps=[];errors=[];archives=[]
def parse_report(raw,source):
 try:d=json.loads(raw.decode('utf-8-sig'))
 except Exception as e:errors.append({'source':source,'error':str(e)});return
 if 'hardware' not in d or 'controlCenter' not in d:return
 hw=d['hardware'];cc=d['controlCenter'];reg=cc.get('gamingCenter2Registry',{})
 services=cc.get('services',[]) or []
 if isinstance(services,dict):services=[services]
 reports.append({'source':source,'sha256':sha(raw),'board':hw.get('baseBoardProduct'),'model':hw.get('productName'),'bios':hw.get('biosVersion'),'registry_ProjectID':reg.get('ProjectID'),'registry_CustomizeTarget':reg.get('CustomizeTarget'),'CC_version':reg.get('AppVersion'),'services':[{'name':s.get('name'),'state':s.get('state'),'service':s.get('gcuService')} for s in services],'firmware_devices':hw.get('firmwareDevices'), 'scope':'OEM registry snapshot, not a direct EC740 read'})
original=ROOT/'research/laptop-reports/telegram-originals'
for p in sorted(original.glob('*.json')):parse_report(p.read_bytes(),str(p))
for p in sorted(original.glob('*.zip')):
 with zipfile.ZipFile(p) as z:
  for i in z.infolist():
   if i.filename.lower().endswith('.json') and i.file_size<2_000_000 and ('Hardware-Report' in i.filename or 'MechrevoPro15' in i.filename):parse_report(z.read(i),str(p)+'!'+i.filename)
def smbios(raw):
 # Windows RawSMBIOSData header is 8 bytes. Keep only model/version fields.
 result={};table=raw[8:8+int.from_bytes(raw[4:8],'little')];pos=0
 while pos+4<=len(table):
  typ,n=table[pos],table[pos+1]
  if n<4 or pos+n>len(table):break
  end=table.find(b'\0\0',pos+n)
  if end<0:break
  strings=table[pos+n:end].split(b'\0')
  def s(off):
   index=table[pos+off] if off<n else 0
   return strings[index-1].decode('utf8',errors='replace') if 0<index<=len(strings) else None
  if typ==0:
   result.update(bios_vendor=s(4),bios=s(5))
   if n>23:result['SMBIOS_EC_version']=f'{table[pos+22]}.{table[pos+23]}'
  elif typ==1:result.update(manufacturer=s(4),model=s(5))
  elif typ==2:result.update(board=s(5))
  pos=end+2
 return result
def inspect(z,source,depth=0):
 names={i.filename.replace('\\','/'):i for i in z.infolist() if not i.is_dir()}
 for name,i in names.items():
  low=name.lower()
  if low.endswith('/ec.bin') or low=='ec.bin':
   if i.file_size!=4096:continue
   folder=str(PurePosixPath(name).parent);prefix='' if folder=='.' else folder+'/'
   validi=names.get(prefix+'ec-valid.bin')
   if not validi:continue
   raw=z.read(i);valid=z.read(validi)
   if len(valid)!=len(raw):errors.append({'source':source+'!'+name,'error':'valid size mismatch'});continue
   identity={};smb=[]
   for stage in ('firmware-before','firmware-after'):
    sp=prefix+stage+'/smbios/windows-rsmb.bin'
    if sp in names:
     b=z.read(names[sp]);identity=smbios(b);smb.append({'member':sp,'sha256':sha(b)});break
   reportname=prefix+'report.txt'
   excerpt=''
   if reportname in names:excerpt=z.read(names[reportname]).decode('utf-8-sig',errors='replace')[:2400]
   if not identity and excerpt:
    for label,key in [('Производитель','manufacturer'),('Модель','model'),('Плата','board')]:
     m=re.search(r'^'+label+r':\s*(.+)$',excerpt,re.M)
     if m:identity[key]=m[1].strip()
    if identity:identity['identity_source']='same-folder report.txt; no raw SMBIOS in this capture'
   # Older captures may not contain RawSMBIOSData. Preserve original report.
   addresses=(0x454,0x456,0x740,0x74c,0x770,0x771,0x772,0x77e,0x77f,0x78e,0x7c3,0x7c8,0x7ca,0x7d2,0x7d3)
   values={f'{a:03X}':raw[a] if valid[a] else None for a in addresses}
   dumps.append({'source':source+'!'+name,'sha256':sha(raw),'valid_sha256':sha(valid),'valid_bytes':sum(bool(v) for v in valid),'identity':identity,'SMBIOS_evidence':smb,'bytes':values,'ROM_770_77F':' '.join(f'{raw[a]:02X}' if valid[a] else '??' for a in range(0x770,0x780)),'report_excerpt':excerpt,'scope':'Saved RAM snapshot after capture started; not automatically factory/pre-application state'})
  elif depth<1 and low.endswith('.zip') and 'dump' in low and i.file_size<30_000_000:
   try:
    with zipfile.ZipFile(io.BytesIO(z.read(i))) as nested:inspect(nested,source+'!'+name,depth+1)
   except zipfile.BadZipFile:pass
for p in sorted((ROOT/'Users_logs').glob('*.zip')):
 if p.stat().st_size>200_000_000:continue # CC installers are a different corpus.
 try:
  with zipfile.ZipFile(p) as z:inspect(z,str(p))
  archives.append(str(p))
 except Exception as e:errors.append({'source':str(p),'error':str(e)})
# Owner WUJIE loose captures: same fields, no hardware I/O.
for p in sorted((ROOT/'Users_logs').rglob('ec.bin')):
 q=p.with_name('ec-valid.bin')
 if p.stat().st_size!=4096 or not q.is_file():continue
 raw=p.read_bytes();valid=q.read_bytes();ident={}
 for stage in ('firmware-before','firmware-after'):
  sm=p.parent/stage/'smbios/windows-rsmb.bin'
  if sm.is_file():ident=smbios(sm.read_bytes());break
 if len(valid)!=4096:continue
 dumps.append({'source':str(p),'sha256':sha(raw),'valid_sha256':sha(valid),'valid_bytes':sum(bool(v) for v in valid),'identity':ident,'bytes':{f'{a:03X}':raw[a] if valid[a] else None for a in (0x454,0x456,0x740,0x74c,0x770,0x771,0x772,0x77e,0x77f,0x78e,0x7c3,0x7c8,0x7ca,0x7d2,0x7d3)},'ROM_770_77F':' '.join(f'{raw[a]:02X}' if valid[a] else '??' for a in range(0x770,0x780)),'scope':'Saved loose RAM snapshot; not automatically factory state'})
result={'hardware_execution':False,'reports':reports,'dumps':dumps,'errors':errors,'archives_inspected':archives}
(OUT/'observations.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('reports',len(reports),'dump occurrences',len(dumps),'unique dump hashes',len({x['sha256'] for x in dumps}),'errors',errors)
by=collections.defaultdict(list)
for d in dumps:by[d['identity'].get('board','UNBOUND')].append(d)
for board,items in sorted(by.items()):
 print(board,len(items),'740',sorted({str(x['bytes']['740']) for x in items}),'74C',sorted({str(x['bytes']['74C']) for x in items}),'7C3',sorted({str(x['bytes']['7C3']) for x in items}),'770',sorted({str(x['bytes']['770']) for x in items}))
groups=collections.defaultdict(set)
for r in reports:
 for s in r['services']:
  if isinstance(s.get('service'),dict):groups[s['service'].get('sha256','UNKNOWN')].add(r['board'])
print('GCUService exact SHA groups:')
for h,boards in groups.items():print(h,sorted(boards))
