"""Offline evidence only: read saved source, image metadata, and logs. No device APIs."""
from pathlib import Path
import hashlib,json,re

root=Path(__file__).resolve().parents[4]
out=Path(__file__).resolve().parent
sources={
 'oem_wmi':('../Decompile/GM6IR0C/Control_Center/0200033E-MyControlCenter.WMIEC.cs',[(59,68),(139,144),(303,311)]),
 'oem_dispatch':('../Decompile/GM6IR0C/Control_Center/020002B9-MyControlCenter.MyFanCtrl.cs',[(190,197)]),
 'oem_manager':('../Decompile/GM6IR0C/Control_Center/020003F0-MyControlCenter.MyFan.MyFanManager_RamFan1p5.cs',[(719,790),(874,943),(2295,2365),(2537,2610),(3331,3405)]),
 'oem_light':('../Decompile/GM6IR0C/Control_Center/0200041C-MyControlCenter.MyFan.FanTable.MyFanTableCtrl.cs',[(139,203)]),
 'oem_app':('../Decompile/GM6IR0C/Control_Center/02000341-MyControlCenter.App.cs',[(434,453),(460,481),(578,583)]),
 'oem_presence':('../Decompile/GM6IR0C/Control_Center/020000EC-MyECIO.MyEcCtrl.cs',[(304,312)]),
 'dsdt':('build/research-gm6ir0c/samples/DSDT.dsl',[(133238,133260),(133272,133292),(134516,134520),(134595,134610),(135173,135183)]),
 'our_subscriptions':('src/platform/windows/OemEventMonitor.cpp',[(50,74)]),
 'our_dispatch':('src/gui/windows/MainWindow.cpp',[(805,819),(1053,1075)]),
 'our_controller':('src/devices/mechrevo/gm6ir0c/ControlWindows.cpp',[(91,105),(167,176),(207,214),(372,399),(685,703),(715,722)]),
}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
manifest=[]; excerpts=[]
for key,(name,ranges) in sources.items():
 p=root/name;s=p.read_text(encoding='utf-8-sig').splitlines()
 manifest.append({'role':key,'path':name,'bytes':p.stat().st_size,'sha256':sha(p),'ranges':ranges})
 excerpts.append('\n### '+key+' — '+name+'\n')
 for start,end in ranges:
  excerpts.extend(f'{n}: {s[n-1]}'.rstrip() for n in range(start,min(end,len(s))+1))
for name in ['build/research-gm6ir0c/samples/GCUService-active.exe','build/research-gm6ir0c/samples/DSDT.aml','build/research-gm6ir0c/samples/companion.log']:
 p=root/name
 manifest.append({'role':'primary_saved_input','path':name,'bytes':p.stat().st_size,'sha256':sha(p)})
photo=Path('C:/Users/admin/AppData/Local/Temp/codex-clipboard-e17326de-d31b-4d1a-9bed-87cdcfcbaf3f.png')
manifest.append({'role':'owner_button_photo','path':str(photo),'bytes':photo.stat().st_size,'sha256':sha(photo),'copied':False})
log=root/'build/gm6ir0c-logs5-20261021/logs_GM6IR0C/MagicControl-1.jsonl'
manifest.append({'role':'new_saved_log','path':str(log.relative_to(root)),'bytes':log.stat().st_size,'sha256':sha(log)})
transitions=[];commands=[];subscriptions=[];events=[];last=None
for line,text in enumerate(log.read_text(encoding='utf-8-sig').splitlines(),1):
 row=json.loads(text);message=row.get('message','')
 if not isinstance(message,str):continue
 item={'line':line,'utc':row['utc'],'channel':row.get('channel'),'message':message}
 if message.startswith('OEM event'):subscriptions.append(item)
 if message.startswith('OEM event:'):events.append(item)
 if row.get('channel')!='gm6ir0c':continue
 if message.startswith('mode='):
  match=re.match(r'(mode=\S+ boost=\S+ custom=\S+ ram=\S+ respective=\S+) PL1/PL2/PL4\(raw\)=(\S+)',message)
  if match and match.group(1)!=last:
   transitions.append(item);last=match.group(1)
 elif message.startswith(('CPU-only EC','CPU-only application','CPU-only Auto','Manual CPU','ONLINE CPU','Default:')):commands.append(item)
result={'scope':'Static saved-file analysis only; no app, device, EC, driver, tests or product build executed.',
 'source_manifest':manifest,'fan_flag_transitions':transitions,'commands':commands,'subscriptions':subscriptions,'captured_oem_events':events,
 'caveats':['Flag status does not expose 727.6 or741.0; LED color is not recorded.','No isolated physical speedometer press was captured.','DSDT OSDF->WMBC uses SAC1/NotifyD2, not SAA1/NotifyD0.']}
(out/'evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(out/'source-excerpts.txt').write_text('\n'.join(excerpts)+'\n',encoding='utf-8')
print(json.dumps({'sources':len(manifest),'flag_transitions':transitions,'commands':len(commands),'subscriptions':subscriptions,'captured_oem_events':len(events)},ensure_ascii=False,indent=2))
