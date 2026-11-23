"""Read extracted saved BIOS sections only. Never executes firmware/capsule."""
from pathlib import Path
import hashlib,json,re,uuid,struct
root=Path(__file__).resolve().parents[5];out=root/'out'/Path(__file__).resolve().parent.relative_to(root)/('inventory-'+uuid.uuid4().hex);out.mkdir(parents=True,exist_ok=False)
dump=root/'research/g9.bin.dump';bios=root/'research/gm6-battery-firmware-20261021/BIOS-MRO09-from-UCP.bin'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
patterns={n:bytes.fromhex(h) for n,h in {'MMIO_FE410000':'000041fe','MMIO_7A6':'a60741fe','MMIO_7B9':'b90741fe','MMIO_7D0':'d00741fe',
 'offset7A6_u32':'a6070000','offset7B9_u32':'b9070000','offset7D0_u32':'d0070000'}.items()}
patterns['esrt_family_guid']=uuid.UUID('13EEDDA5-5552-5DF0-A651-569AA1396B84').bytes_le
patterns['OEM_variable_guid']=uuid.UUID('9f33f85c-13ca-4fd1-9c4a-96217722c593').bytes_le
for s in ['UniWillVariable','OemMagicVariable','OemMagicDoor','BatteryLimitation','ChargeMaximum','ChargeMinimum','Battery','GM6IR0C','GMxIR7x','1.12','MRO09']:
 patterns['ascii_'+s]=s.encode();patterns['utf16_'+s]=s.encode('utf-16-le')
hits=[];modules=[]
for p in sorted(dump.glob('Section_PE32_image_*_body.bin')):
 b=p.read_bytes();found={k:[m.start() for m in re.finditer(re.escape(v),b)] for k,v in patterns.items()};found={k:v for k,v in found.items() if v}
 if found or re.search(r'OemServiceSmm|OemACPIDriverSmm|OemHooksSmm|OemUniWillVariableDxe|OemPowerModeDxe',p.name):
  rec={'path':str(p.relative_to(root)),'bytes':len(b),'sha256':sha(p),'hits':found};hits.append(rec)
 modules.append({'path':str(p.relative_to(root)),'bytes':len(b),'sha256':sha(p)})
dsdt=dump/'Section_Raw_C118F50D-391D-45F4-B3D3-11BC931AA56D_body.bin';b=dsdt.read_bytes();signatures=[]
for m in re.finditer(b'DSDT',b):
 off=m.start()
 if off+36>len(b):continue
 length=struct.unpack_from('<I',b,off+4)[0]
 if not 36<=length<=len(b)-off:continue
 table=b[off:off+length];signatures.append({'offset':off,'length':length,'sha256':hashlib.sha256(table).hexdigest(),
  'checksum_valid':sum(table)%256==0,'header_hex':table[:36].hex(),'oem_id':table[10:16].decode('ascii',errors='replace'),'oem_table_id':table[16:24].decode('ascii',errors='replace')})
comparisons=[]
for p in [root/'research/research-gm6ir0c/samples/DSDT.aml',*sorted((root/'research/devices/gm6ir0c/battery-healthy-20261021/saved-acpi').glob('DSDT*'))]:
 if not p.exists():continue
 saved=p.read_bytes();offsets=[m.start() for m in re.finditer(re.escape(saved),b)]
 diffs=[i for i,(a,z) in enumerate(zip(saved,b)) if a!=z];ranges=[]
 for i in diffs:
  if ranges and i==ranges[-1][-1]+1:ranges[-1].append(i)
  else:ranges.append([i])
 comparisons.append({'path':str(p.relative_to(root)),'bytes':len(saved),'sha256':sha(p),'exact_subsequence_offsets':offsets,
  'different_bytes':len(diffs),'difference_ranges':[{'offset':r[0],'bytes':len(r),'saved':saved[r[0]:r[-1]+1].hex(),'downloaded':b[r[0]:r[-1]+1].hex()} for r in ranges]})
raw_ids=[]
for p in sorted(dump.glob('Section_*_body.bin')):
 if re.search(r'2EBE0275|AB56DC60|207F94A8',p.name,re.I):
  raw=p.read_bytes();strings=[(m.start(),m.group().decode('ascii')) for m in re.finditer(rb'[\x20-\x7e]{5,}',raw)]
  raw_ids.append({'path':str(p.relative_to(root)),'bytes':len(raw),'sha256':sha(p),'strings':strings[:80],'prefix_hex':raw[:64].hex()})
result={'scope':'Saved files only; exact-target identity not inferred from download filename.',
 'bios':{'path':str(bios.relative_to(root)),'bytes':bios.stat().st_size,'sha256':sha(bios)},
 'pe_modules':len(modules),'module_hits':hits,'dsdt_raw':{'path':str(dsdt.relative_to(root)),'bytes':len(b),'sha256':sha(dsdt),'tables':signatures},
 'saved_dsdt_comparison':comparisons,'identity_raw_sections':raw_ids}
(out/'inventory.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(out/'pe-manifest.json').write_text(json.dumps(modules,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'pe_modules':len(modules),'hit_modules':len(hits),'dsdt_tables':signatures,'saved_comparison':comparisons,'identity_sections':[(r['path'],r['bytes'],r['strings'][:10]) for r in raw_ids]},ensure_ascii=False,indent=2))
