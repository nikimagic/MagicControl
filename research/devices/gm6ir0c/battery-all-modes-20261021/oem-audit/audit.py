"""Exact saved OEM audit. No OEM imports/execution, device APIs, or product tests."""
from pathlib import Path
import hashlib,json,struct,re

root=Path(__file__).resolve().parents[5]
out=Path(__file__).resolve().parent
binary=root/'build/research-gm6ir0c/samples/GCUService-active.exe'
metadata=root/'build/research-hydroc16g2/battery-recovery/gm6-metadata.json'
b=binary.read_bytes()
def u16(data,offset):return struct.unpack_from('<H',data,offset)[0]
def u32(data,offset):return struct.unpack_from('<I',data,offset)[0]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
assert sha(binary)=='8fd02bf5f42629998d8ec716c3e6ff906baf73cb356b1fdd7585a26d77a49daf'
pe=u32(b,0x3c);assert b[pe:pe+4]==b'PE\0\0'
sections=[];optional=pe+24;section_start=optional+u16(b,pe+20)
for n in range(u16(b,pe+6)):
 p=section_start+n*40;sections.append((u32(b,p+12),max(u32(b,p+8),u32(b,p+16)),u32(b,p+20)))
def raw(rva):
 for start,size,offset in sections:
  if start<=rva<start+size:return offset+rva-start
 raise ValueError(f'Unmapped RVA {rva:x}')
data_directory=optional+(112 if u16(b,optional)==0x20b else 96)
cli=raw(u32(b,data_directory+14*8));md=raw(u32(b,cli+8));assert b[md:md+4]==b'BSJB'
p=(md+16+u32(b,md+12)+3)&~3;stream_count=u16(b,p+2);p+=4;streams={}
for _ in range(stream_count):
 offset,size=u32(b,p),u32(b,p+4);end=b.index(0,p+8);name=b[p+8:end].decode('ascii');streams[name]=(md+offset,size);p=(end+4)&~3
tables=streams.get('#~',streams.get('#-'))[0];valid=struct.unpack_from('<Q',b,tables+8)[0];p=tables+24;counts={}
for table in range(64):
 if valid&(1<<table):counts[table]=u32(b,p);p+=4
d=json.loads(metadata.read_text(encoding='utf-8-sig'));methods=d['methods'];fields=d['fields']
names_path=out/'token-names.json';names=json.loads(names_path.read_text(encoding='utf-8-sig'))
def name(token):return names.get(str(token),f'0x{token:08X}')
def owner(token):return name(token).split('::')[0]
assert counts[6]==len(methods);assert counts[4]==len(fields)
opcodes={r['code']:r for r in json.loads((out/'cil-opcodes.json').read_text(encoding='utf-8-sig'))}
sizes={'InlineNone':0,'ShortInlineBrTarget':1,'ShortInlineI':1,'ShortInlineVar':1,'InlineVar':2,
 'InlineBrTarget':4,'InlineField':4,'InlineI':4,'InlineMethod':4,'InlineSig':4,'InlineString':4,
 'InlineTok':4,'InlineType':4,'ShortInlineR':4,'InlineI8':8,'InlineR':8}
references=[];literal_references=[];errors=[];verified=0;decoded=0
def user_string(token):
 assert token>>24==0x70
 pos=streams['#US'][0]+(token&0xffffff);first=b[pos];pos+=1
 if first<0x80:length=first
 elif first<0xc0:length=((first&0x3f)<<8)|b[pos];pos+=1
 else:length=((first&0x1f)<<24)|(b[pos]<<16)|(b[pos+1]<<8)|b[pos+2];pos+=3
 return b[pos:pos+max(0,length-1)].decode('utf-16-le',errors='replace')
for method in methods:
 if not method.get('rva') or not method.get('il'):continue
 code=bytes.fromhex(method['il']);body=raw(method['rva']);kind=b[body]&3
 if kind==2:header=1;length=b[body]>>2
 elif kind==3:header=(u16(b,body)>>12)*4;length=u32(b,body+4)
 else:errors.append({'token':method['token'],'error':'non-IL method header'});continue
 if length!=len(code) or b[body+header:body+header+length]!=code:raise ValueError(f'IL provenance mismatch: {method}')
 verified+=1;p=0
 try:
  while p<len(code):
   at=p;opcode=code[p];p+=1
   if opcode==0xfe:opcode=(opcode<<8)|code[p];p+=1
   spec=opcodes[opcode];operand=spec['operand']
   size=4+4*u32(code,p) if operand=='InlineSwitch' else sizes[operand]
   if p+size>len(code):raise ValueError('truncated instruction')
   if operand in ('InlineMethod','InlineField','InlineTok','InlineType'):
    target=u32(code,p)
    references.append({'caller_token':method['token'],'caller_type':owner(method['token']),'caller_name':method['name'],
      'offset':at,'opcode':spec['name'],'target':target,'target_name':name(target)})
   elif operand=='InlineString':
    literal_references.append({'caller_token':method['token'],'caller_type':owner(method['token']),'caller_name':method['name'],
      'offset':at,'literal':user_string(u32(code,p))})
   p+=size
  decoded+=1
 except Exception as error:errors.append({'token':method['token'],'type':method['type'],'name':method['name'],'offset':p,'error':str(error)})
assert not errors,errors
by_token={m['token']:m for m in methods}
interesting_types=['GCUService.MySystem.BatteryProtection2','GCUService.MySystem.BatteryProtection','GCUService.MySystem.BatteryPercentManger']
method_audit=[]
for m in methods:
 if m['type'] not in interesting_types:continue
 refs=[r for r in references if r['target']==m['token']]
 method_audit.append({'token':f"0x{m['token']:08X}",'type':m['type'],'name':m['name'],'rva':m.get('rva'),
  'il_sha256':hashlib.sha256(bytes.fromhex(m.get('il',''))).hexdigest(),'references':refs,
  'external_references':[r for r in refs if not (r['caller_type']==m['type'] or r['caller_type'].startswith(m['type']+'+'))]})
legacy='GCUService.MySystem.BatteryProtection'
def old_name(n):return n==legacy or n.startswith(legacy+'::') or n.startswith(legacy+'+')
old_external=[r for r in references if old_name(r['target_name']) and not old_name(r['caller_type'])]
charging_field_refs={}
for suffix in ('BatteryLimitation','ChargeMaximumLimit','ChargeMinimumLimit'):
 matching={int(t) for t,n in names.items() if n.startswith('MyControlCenter.NVRAM_STRUCT::'+suffix+' :')}
 charging_field_refs[suffix]=[r for r in references if r['target'] in matching]
selected_extra=[]
for m in methods:
 if (m['type']=='MyControlCenter.MyFanManager_QC' and m['name'] in ('.ctor','SetBatteryChargingLimit','ReadBatteryChargingLimit','UserSet_BatteryChargingLimit_Setting')) or \
    (m['type']=='MyControlCenter.CustomizeCtrl' and m['name'] in ('DetectQCRefreshFeatures','IsQCRefreshBIOS')):
  selected_extra.append({'name':name(m['token']),'token':m['token'],'references':[r for r in references if r['target']==m['token']]})
dynamic_names=['SetBatteryChargingLimit_Up','SetBatteryChargingLimit_Down','ReadBatteryChargingLimit_Up','ReadBatteryChargingLimit_Down',
 'm_BatteryChargingLimit_Up','m_BatteryChargingLimit_Down','m_BatteryLimitationMode','LoadBatteryLimitationDefault',
 'BatteryProtection','BatteryProtection2','MyFanManager_QC','DetectQCRefreshFeatures','UserSet_BatteryChargingLimit_Setting']
dynamic_audit={n:[r for r in literal_references if r['literal']==n] for n in dynamic_names}
audit={'scope':'Offline exact saved managed OEM only; never loaded as an executable or assembly.',
 'binary':{'path':str(binary.relative_to(root)),'sha256':sha(binary),'bytes':len(b)},
 'metadata':{'path':str(metadata.relative_to(root)),'sha256':sha(metadata),'methods':len(methods),'fields':len(fields)},
 'fresh_token_names':{'path':str(names_path.relative_to(root)),'sha256':sha(names_path),'count':len(names)},
 'fresh_PE_table_counts':counts,'raw_IL_verified_against_exact_binary':verified,'fully_decoded_method_bodies':decoded,'decode_errors':errors,
 'reference_instruction_count':len(references),'method_audit':method_audit,'legacy_class_external_method_field_type_references':old_external,
 'charging_NVRAM_field_references':charging_field_refs,'platform_gate_reference_audit':selected_extra,
 'ldstr_instruction_count':len(literal_references),'dynamic_binder_member_literal_references':dynamic_audit,
 'limitations':['Direct CIL tokens in this exact assembly; excludes absent firmware and external reflection/native code.',
  'Existing metadata names checked through saved decompilation; each method IL and RVA independently matched exact PE bytes; no token-byte substring search.']}
(out/'cil-reference-audit.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
inputs={
 'new_binary_reference_audit':[binary,metadata,names_path,out/'cil-opcodes.json',root/'build/research-hydroc16g2/decompiler/bin/Debug/net10.0/Decompile.dll',
  root/'build/research-hydroc16g2/decompiler/Program.cs',root/'build/research-hydroc16g2/decompiler/MetaNames.cs'],
 'prior_reports':[root/'research/devices/gm6ir0c/battery-healthy-20261021/README.md',root/'research/devices/gm6ir0c/percentage-search-20261021/REPORT.txt'],
 'source_excerpts':[]}
selections={
 '../Decompile/GM6IR0C/Control_Center/020001C6-GCUService.MySystem.BatteryProtection2.cs':[(15,30),(87,151),(154,235),(237,312),(324,371),(374,439),(446,479)],
 '../Decompile/GM6IR0C/Control_Center/020001BB-GCUService.MySystem.BatteryProtection.cs':[(274,344),(346,443),(467,611),(679,689),(715,790)],
 '../Decompile/GM6IR0C/Control_Center/020001B8-GCUService.MySystem.BatteryPercentManger.cs':[(1,70)],
 '../Decompile/GM6IR0C/Control_Center/02000341-MyControlCenter.App.cs':[(180,193),(350,360),(414,420),(464,470),(578,590),(645,657)],
 '../Decompile/GM6IR0C/Control_Center/020002B9-MyControlCenter.MyFanCtrl.cs':[(25,74)],
 '../Decompile/GM6IR0C/Control_Center/0200022C-MyControlCenter.CustomizeCtrl.cs':[(260,325)],
 '../Decompile/GM6IR0C/Control_Center/02000254-MyControlCenter.MyFanManager_QC.cs':[(39,45),(548,558),(861,888),(1808,1833)],
 '../Decompile/GM6IR0C/Control_Center/0200023A-MyControlCenter.NvramVariable.cs':[(60,75),(316,376)],
 '../Decompile/GM6IR0C/Control_Center/02000239-MyControlCenter.NVRAM_STRUCT.cs':[(50,71)],
 '../Decompile/GM6IR0C/Control_Center/020000EA-MyECIO.AcpiCtrl.cs':[(186,227),(376,394)],
 '../Decompile/GM6IR0C/Control_Center/020000EC-MyECIO.MyEcCtrl.cs':[(34,42),(253,256),(304,312)],
 '../Decompile/GM6IR0C/Control_Center/020002CF-MyControlCenter.MySettingManager.cs':[(1115,1124),(1961,1980)],
 '../Decompile/GM6IR0C/Control_Center/020003F0-MyControlCenter.MyFan.MyFanManager_RamFan1p5.cs':[(2295,2317),(2859,2874)],
 '../Decompile/GM6IR0C/Control_Center/02000331-MyControlCenter.PowerModeEvent.cs':[(26,73)],
 '../Decompile/GM6IR0C/Control_Center/02000099-Workaround.WKD_CriticalBatterySetting.cs':[(1,44)],
 'build/research-gm6ir0c/decompiled-companion/Companion.Core/PowerCommands.cs':[(13,44)],
 'build/research-gm6ir0c/decompiled-companion/Companion/AppController.cs':[(787,800),(1302,1318)],
 'build/research-gm6ir0c/decompiled-companion/Companion.Ui.Pages.Power.Strings.resx':[(40,75)],
 'build/research-gm6ir0c/battery-ui-resources/resources.pri.xml':[(2950,2965),(2982,3005),(3014,3036)],
 'src/platform/windows/NativeBatteryWindows.cpp':[(195,245),(282,298)],
 'src/gui/windows/SettingsPage.cpp':[(1467,1484)]}
excerpts=[]
for file,ranges in selections.items():
 path=root/file;lines=path.read_text(encoding='utf-8-sig').splitlines();inputs['source_excerpts'].append(path)
 excerpts.append({'path':file,'sha256':sha(path),'ranges':[{'first':a,'last':min(z,len(lines)),
  'lines':[{'line':n,'text':lines[n-1]} for n in range(a,min(z,len(lines))+1)]} for a,z in ranges]})
(out/'source-excerpts.json').write_text(json.dumps(excerpts,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
manifest={category:[{'path':str(p.relative_to(root)),'sha256':sha(p),'bytes':p.stat().st_size} for p in paths] for category,paths in inputs.items()}
(out/'inputs.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
zero=[f"{x['type']}::{x['name']}" for x in method_audit if not x['references']]
print(json.dumps({'verified':verified,'decoded':decoded,'errors':errors,'legacy_external':old_external,'zero_reference_methods':zero},ensure_ascii=False,indent=2))
