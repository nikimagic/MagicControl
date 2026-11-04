"""Offline GM6 Healthy call-chain evidence. No OEM loading, device or registry APIs."""
from pathlib import Path
import hashlib,json,re,zipfile,collections,datetime
ROOT=Path(__file__).resolve().parents[4]
OUT=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
base=ROOT/'build/research-gm6ir0c'
sources={
 'active_service':base/'samples/GCUService-active.exe',
 'driver':base/'samples/UWACPIDriver.sys',
 'dsdt_aml':base/'samples/DSDT.aml','dsdt_asl':base/'samples/DSDT.dsl',
 'service_entry':base/'all-active/02000341-MyControlCenter.App.cs',
 'policy':base/'all-active/020001C6-GCUService.MySystem.BatteryProtection2.cs',
 'battery_timer':base/'all-active/020001B8-GCUService.MySystem.BatteryPercentManger.cs',
 'ec_wrapper':base/'all-active/020000EC-MyECIO.MyEcCtrl.cs',
 'transport':base/'all-active/020000EA-MyECIO.AcpiCtrl.cs',
 'nvram_writer':base/'all-active/0200023A-MyControlCenter.NvramVariable.cs',
 'companion_command':base/'decompiled-companion/Companion.Core/PowerCommands.cs',
 'companion_ui':base/'decompiled-companion/Companion.Ui.Pages.Power/PowerPage.xaml.cs',
 'oem_ui_resources':base/'battery-ui-resources/resources.pri.xml',
 'driver_disassembly':ROOT/'build/gm6-healthy-driver-original.asm',
 'current_percentage_writer':ROOT/'src/platform/windows/NativeBatteryWindows.cpp',
 'saved_uefi_variable':OUT/'UniWillVariable.bin',
}
evidence={'scope':'Static source/PE/ASL and saved log files only. No application/OEM/driver/EC/SMBus/registry execution or tests.',
 'sources':{k:{'path':str(p.relative_to(ROOT)).replace('\\','/'),'bytes':p.stat().st_size,'sha256':sha(p)}for k,p in sources.items()},
 'host_chain':['Companion Stationary -> PowerCommands.SetBatteryMode -> MQTT BatteryProtection/Control Action HEALTHYMODE',
 'App.Receive -> BatteryProtection2.Receive -> HealthProtectionStatus=2 persisted in OEM registry -> SetHealthProtectionLow',
 'fresh read7A6; clear bit4 and set bit5, preserve all other bits',
 'MyEcCtrl.Write -> AcpiCtrl.Write -> WriteACPI IOCTL0x9C40A48C, two DWORD addr/value',
 'Archived GM6 UWACPIDriver dispatch29CB ->2B04 -> helper25B8 -> ACPI ECRW (method signature57524345h)',
 'DSDT _SB.INOU.ECRW -> MMRW(FE410000+offset,write=1,width=byte,value) -> physical byte FE4107A6',
 'EC executable consumer/charger control: NOT obtained; no numeric voltage/SOC/hysteresis algorithm proven'],
 'native_policy_modes':{'0':{'mask30_hex':'00','name':'Performance/FullCapacity'},'1':{'mask30_hex':'10','name':'Balanced'},'2':{'mask30_hex':'20','name':'Healthy/Stationary'}},
 'lifecycle':{'startup':'Init reads saved HealthProtectionStatus, applies mode; default0 except CustomizeTarget17 default1',
 'resume':'Resume reapplies selection; PowerModes.Resume calls Init to reload saved selection',
 'ac_change':'Updates UI only in BatteryProtection2 handler',
 'disable_uninstall_recovery':'SetHealthProtectionHigh clears mode bits; Disable does not erase registry selection',
 'timer':'BatteryPercentManger10000ms polls SOC and raises UI event only; no charge algorithm'},
 'unproven':['Healthy cutoff voltage and SOC','restart/hysteresis','dependency on APP presence741.0','percentage activation/firmware consumer','persistence of physical policy across service exit/BIOS reset']}
policy=sources['policy'].read_text(encoding='utf-8-sig')
evidence['percentage_method_references']={}
for name in ('SetBatteryChargingLimit_Up','SetBatteryChargingLimit_Down','LoadBatteryLimitationDefault'):
 hits=[]
 for p in (base/'all-active').glob('*.cs'):
  for line,text in enumerate(p.read_text(encoding='utf-8-sig').splitlines(),1):
   if re.search(r'\b'+name+r'\s*\(',text):hits.append({'file':p.name,'line':line,'text':text.strip()})
 evidence['percentage_method_references'][name]=hits
with zipfile.ZipFile(base/'Mechrevo-Research-GM6IR0C-20261017-215647.zip')as z:
 member=next(x for x in z.namelist()if x.endswith('registry/oem-registry.json'))
 raw=z.read(member);todo=[json.loads(raw.decode('utf-8-sig'))];found=[]
 while todo:
  x=todo.pop()
  if isinstance(x,list):todo.extend(x)
  elif isinstance(x,dict):
   if x.get('name')=='HealthProtectionStatus':found.append(x)
   todo.extend(v for v in x.values()if isinstance(v,(dict,list)))
 evidence['original_saved_policy']={'member':member,'member_sha256':hashlib.sha256(raw).hexdigest(),'entries':found}
 evidence['original_firmware_payload_names']=[i.filename for i in z.infolist()if Path(i.filename).suffix.lower()in ('.rom','.fd','.cap')]
dumps=[]
for p in (ROOT/'build/gm6ir0c-logs4-20261021/logs_GM6IR0C').rglob('ec.bin'):
 b=p.read_bytes();dumps.append({'path':str(p.relative_to(ROOT)).replace('\\','/'),'sha256':sha(p),
 'raw':{f'{a:03X}':b[a]for a in (0x741,0x7A6,0x7B9,0x7D0)},'mode':(b[0x7A6]>>4)&3})
evidence['saved_ram_contexts']=dumps
(OUT/'source-excerpts.json').write_text(json.dumps({
 'scope':'Short static source excerpts, accompanied by full input hashes; never executed.',
 'excerpts':[{'source':str(sources[key].relative_to(ROOT)).replace('\\','/'),'start_line':start,'end_line':end,
 'text':'\n'.join(sources[key].read_text(encoding='utf-8-sig').splitlines()[start-1:end])}
 for key,start,end in [('policy',149,184),('policy',244,251),('policy',282,313),('policy',374,411),
 ('battery_timer',19,27),('battery_timer',54,65),('transport',207,225),('transport',376,390),
 ('ec_wrapper',304,313),('companion_command',25,35),('dsdt_asl',131879,131930),('dsdt_asl',131955,131968),
 ('service_entry',411,420),('service_entry',579,588)]]},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(OUT/'evidence.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

logs=ROOT/'build/gm6ir0c-logs5-20261021/logs_GM6IR0C'
read=lambda p:[json.loads(x)for x in p.read_text(encoding='utf-8-sig').splitlines()if x.strip()]
main=read(logs/'MagicControl-1.jsonl');battery=read(logs/'battery-1.jsonl')
def msk(t):return datetime.datetime.fromisoformat(t.replace('Z','+00:00')).astimezone(datetime.timezone(datetime.timedelta(hours=3))).isoformat()
snapshots=[]
for r in battery:
 msg=str(r.get('message',''))
 if r.get('channel')=='battery_candidates'and msg.startswith('SNAPSHOT'):
  fields={int(a,16):int(v,16)for a,v in re.findall(r'0x([0-9A-F]{3})=(0x[0-9A-F]+)/',msg)}
  fields.update({int(a,16):int(v,16)for a,v in re.findall(r'addr=0x([0-9A-F]{3})[^;]*?value=(0x[0-9A-F]+)',msg)})
  snapshots.append({'utc':r['utc'],'time_msk':msk(r['utc']),'summary':msg.split(' fields=')[0],
  'raw':{f'{a:03X}':v for a,v in fields.items()if a in (0x432,0x434,0x436,0x438,0x7A6,0x7B9,0x7D0)}})
commands=[{'utc':r['utc'],'time_msk':msk(r['utc']),'line':i,'message':r['message']}for i,r in enumerate(main,1)
 if r.get('channel')in ('battery','gm6ir0c')and ('EXPERIMENT'in str(r.get('message',''))or'CPU-only'in str(r.get('message',''))or'ONLINE'in str(r.get('message',''))or'Default:'in str(r.get('message','')))]
summary={'archive':json.loads((ROOT/'research/gm6ir0c-logs5-20261021/archive-manifest.json').read_text(encoding='utf-8')),
 'scope':'Saved logs0.1.129 only. No hardware actions executed by analysis.',
 'main_rows':len(main),'battery_rows':len(battery),'first_msk':msk(main[0]['utc']),'last_msk':msk(main[-1]['utc']),
 'same_session':main[0]['session']==battery[0]['session'],'same_slot':main[0]['slot']==battery[0]['slot'],
 'battery_channels':dict(collections.Counter(r['channel']for r in battery)),'register_snapshots':snapshots,'commands':commands,
 'conclusion':'Percentage85/80 configured; native mode stays Performance throughout snapshots. Charging resumes at99% and returns100%; Healthy was not selected. CPU nonzero overrides retained until explicit later commands; physical Intel PL enforcement remains unmeasured.'}
(ROOT/'research/gm6ir0c-logs5-20261021/evidence.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'source_hashes':len(sources),'original_saved_policy':found,'original_firmware_payloads':len(evidence['original_firmware_payload_names']),
 'logs5_main':len(main),'logs5_battery':len(battery),'same_session':summary['same_session'],'raw_snapshots':len(snapshots),'raw_policy_values':sorted({s['raw']['7A6']for s in snapshots if '7A6'in s['raw']})},ensure_ascii=False))
