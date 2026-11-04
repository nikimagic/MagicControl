"""Static identity/AML method comparison. No firmware execution or live firmware APIs."""
from pathlib import Path
import hashlib,json,re,struct
root=Path(__file__).resolve().parents[5];out=Path(__file__).resolve().parent;dump=root/'build/g9.bin.dump'
sha=lambda b:hashlib.sha256(b).hexdigest()
new=(dump/'Section_Raw_C118F50D-391D-45F4-B3D3-11BC931AA56D_body.bin').read_bytes()
targets=[root/'build/research-gm6ir0c/samples/DSDT.aml',root/'research/devices/gm6ir0c/battery-healthy-20261021/saved-acpi/DSDT-1c2b99426121.aml']
def methods(data):
 result=[]
 for pos in range(len(data)-8):
  if data[pos]!=0x14:continue
  count=data[pos+1]>>6;length=data[pos+1]&(15 if count else 63)
  for n in range(count):length|=data[pos+2+n]<<(4+8*n)
  body=pos+2+count;end=pos+1+length
  if not body+5<=end<=len(data):continue
  name=data[body:body+4]
  if name not in [b'_BIF',b'_BST',b'_PSR',b'ECRW',b'MMRW',b'_Q53']:continue
  code=data[pos:end]
  result.append({'name':name.decode(),'offset':pos,'bytes':len(code),'sha256':sha(code),'hex':code.hex()})
 return result
new_methods=methods(new);comparisons=[]
for p in targets:
 old=p.read_bytes();old_methods=methods(old)
 comparisons.append({'path':str(p.relative_to(root)),'sha256':sha(old),'method_comparisons':[
  {'name':m['name'],'offset':m['offset'],'bytes':m['bytes'],'sha256':m['sha256'],
   'same_offset_identical_in_firmware':any(n['offset']==m['offset'] and n['sha256']==m['sha256'] for n in new_methods)} for m in old_methods]})
ecpath=dump/'Section_Raw_207F94A8-238E-11E8-B467-0ED5F89F718B_body.bin';ec=ecpath.read_bytes()
sequence=bytes.fromhex('e4909ccc93900454f0');where=[m.start() for m in re.finditer(sequence,ec)]
version={'candidate_ec_sha256':sha(ec),'init_sequence':sequence.hex(),'init_physical_offsets':where,
 'fixed_common_bytes':32768,'banked_window_logical_start':32768,'selected_window_bank':1,
 'mapping':'fixed0..7FFF + selected slice[bank*8000+8000:bank*8000+10000]',
 'logical_constant':0x9ccc,'physical_constant':0x11ccc,'raw_version_byte':ec[0x11ccc],
 'major_shift6':ec[0x11ccc]>>6,'minor_mask3f':ec[0x11ccc]&63,
 'basis':'Unique initializer itself executes in selected bank1 window at logical84EC (physical104EC), no bank switch before MOVC9CCC; SmbiosBoard868 reads EC0454 and splits >>6/&3F;924 writes Type0offset16/17.',
 'limit':'Banked architecture mapping corroborates root audit. Not a byte-for-byte match to the live EC flash; no flash dump was taken.'}
inventory=root/'build/gm6ir0c-logs4-20261021/logs_GM6IR0C/dumps/dump-20261021-175302-0/inventory.json'
live=json.loads(inventory.read_text(encoding='utf-8-sig'));live_identity=[];stack=[('',live)]
while stack:
 key,value=stack.pop()
 if isinstance(value,dict):
  if value.get('value') in ['13EEDDA5-5552-5DF0-A651-569AA1396B84','1.12','N.1.05MRO09']:
   live_identity.append({'json_path':key,'row':value})
  stack.extend((key+'.'+str(k),v) for k,v in value.items())
 elif isinstance(value,list):stack.extend((key+'['+str(n)+']',v) for n,v in enumerate(value))
result={'scope':'Candidate BIOS identity, not permission to flash or equivalence of live EC1.12.',
 'firmware_dsdt_sha256':sha(new),'firmware_methods':new_methods,'saved_method_comparison':comparisons,
 'candidate_ec_version':version,'saved_live_inventory':{'path':str(inventory.relative_to(root)),'sha256':sha(inventory.read_bytes()),'selected_rows':live_identity}}
(out/'identity-evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
selected=[];export=root/'build/gm6-bios-static-export'
choices={'SmbiosBoard':['00000868.c','00000924.c','00000b28.c'],
 'OemUniWillVariableDxe':['00000528.c'],'OemPowerModeDxe':['00000568.c'],
 'OemServiceSmm':['000062f4.c','00006430.c','00006728.c'],'OemACPIDriverSmm':['00001590.c'],'OemHooksSmm':['000012bc.c']}
for folder in export.iterdir():
 if not folder.is_dir():continue
 for marker,names in choices.items():
  if '_'+marker+'_body.bin' not in folder.name:continue
  for name in names:
   p=folder/name
   if p.exists():selected.append({'module':folder.name,'function_rva':name[:-2],'path':str(p.relative_to(root)),
    'sha256':sha(p.read_bytes()),'lines':[{'line':n,'text':line} for n,line in enumerate(p.read_text().splitlines(),1)]})
(out/'decompiled-evidence.json').write_text(json.dumps(selected,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
manifest=[]
for folder in export.iterdir():
 if not folder.is_dir():continue
 p=dump/folder.name;outputs=sorted(folder.glob('*.c'));errors=list(folder.glob('*.error.txt'))
 manifest.append({'module':folder.name,'input_bytes':p.stat().st_size,'input_sha256':sha(p.read_bytes()),'decompiled_functions':len(outputs),
  'decompile_errors':len(errors),'output_files':[{'name':f.name,'sha256':sha(f.read_bytes())} for f in outputs]})
(out/'decompiler-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'method_comparison':comparisons,'candidate_version':version,'saved_rows':live_identity,
 'modules':len(manifest),'functions':sum(m['decompiled_functions'] for m in manifest)},ensure_ascii=False,indent=2))
