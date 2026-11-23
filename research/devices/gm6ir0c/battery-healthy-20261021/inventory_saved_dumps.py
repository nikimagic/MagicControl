from pathlib import Path
import subprocess,json,zipfile,collections,uuid
PROJECT=Path(__file__).resolve().parents[4]
RUN=PROJECT/'out'/('saved-firmware-inventory-'+uuid.uuid4().hex)
RUN.mkdir(parents=True,exist_ok=False)
roots=[Path(r'C:\Users\admin\Documents\mechrevo_dsh'),Path(r'C:\Users\admin\Documents\mechrevo')]
names=subprocess.check_output(['rg','--files','--hidden','--no-ignore','-g','!**/.git/**','-g','!**/node_modules/**',*[str(p)for p in roots]],text=True,encoding='utf-8').splitlines()
exts={'.bin','.rom','.fd','.cap','.dump','.spi','.img','.hex','.dat'}
counts=collections.Counter();large=[];gm6=[]
for name in names:
 p=Path(name)
 if p.suffix.lower()not in exts:continue
 try:size=p.stat().st_size
 except OSError:continue
 counts[p.suffix.lower()]+=1
 if size<65536:continue
 with p.open('rb')as f:head=f.read(4096)
 row={'path':str(p),'bytes':size,'head16_hex':head[:16].hex(),'fv_header_in_prefix':b'_FVH'in head,
 'intel_descriptor_signature_at10':head[16:20]==b'\x5a\xa5\xf0\x0f','pe':head.startswith(b'MZ')}
 large.append(row)
 if 'gm6'in name.lower():gm6.append(row)
archives=[]
for p in Path(r'C:\Users\admin\Downloads\Telegram Desktop').glob('*.zip'):
 if not('GM6'in p.name or p.name.startswith('dump-')):continue
 with zipfile.ZipFile(p)as z:
  binary=[{'member':i.filename,'bytes':i.file_size}for i in z.infolist()if Path(i.filename).suffix.lower()in exts]
  reports=[(i.filename,z.read(i)[:1800].decode('utf-8-sig',errors='replace'))for i in z.infolist()if i.filename.endswith('report.txt')]
  profiles=sorted({line for _,text in reports for line in text.splitlines()if line.startswith(('Профиль:','Плата:','Версия:'))})
  archives.append({'path':str(p),'entries':len(z.infolist()),'binary_members':binary,'profiles':profiles})
out={'roots':[str(p)for p in roots],'binary_counts':dict(counts),'large_binary_files':large,'gm6_large_binary_files':gm6,'archives':archives}
(RUN/'saved-firmware-inventory.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print('counts',dict(counts),'large',len(large),'gm6_large',len(gm6))
print('GM6 binaries >=64KiB',json.dumps(gm6,ensure_ascii=False))
for a in archives:print('ARCHIVE',Path(a['path']).name,a['entries'],a['profiles'],'EC files',[(x['member'],x['bytes'])for x in a['binary_members']if Path(x['member']).name in ('ec.bin','ec-after.bin')], 'large_non_acpi',[(x['member'],x['bytes'])for x in a['binary_members']if x['bytes']>=65536 and '/acpi/'not in x['member'].lower()])
print('Potential unclassified large binaries',json.dumps([x for x in large if not any(s in x['path'].lower()for s in ('pawnio','node_modules','ec8000','ec9000','font','package','site-packages','pypi','gcu-by-token'))][:30],ensure_ascii=False))
