"""Prepare evidence rows for the requested one-line-per-model CSV."""
from pathlib import Path
import json,re,csv,collections,hashlib
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
CAT=ROOT/'research/ec-platform-catalogue-20261023'
inv=json.loads((HERE/'source-inventory.json').read_text(encoding='utf-8'))
N='Не установлено'
headers=['Ноутбук / семейство','Вид записи','Плата / модель SMBIOS','Процессор','Видеокарта','Control Center / корпус','Project ID EC [740]','Уточнённый ProjectID СС','BIOS_PROJECT_ID (другой справочник)','Платформа EC [7C3]','ROM ID / условия выбора','BIOS: известные версии','EC: сообщённые версии','EC: имеющиеся образы','Механизм определения / подключения','Возможности из кода СС','Что не подтверждено','Источники (файл:строка)','Дата каталога']
rows=[]
def row(name,kind,**kw):
 r={h:N for h in headers};r.update({'Ноутбук / семейство':name,'Вид записи':kind,'Дата каталога':'26.10.2026'});r.update(kw);rows.append(r);return r
def at(x):return x['path']+':'+str(x['line'])
def packages(items):
 out=set()
 for x in items:
  p=x['path'];m=re.search(r'oem-(\d+(?:\.\d+){2,3})',p)
  if m:out.add('CC '+m[1])
  if 'Reference_w568w' in p:out.add('GCUService reference_w568w (отдельный OEM)')
  elif '\\Decompiled\\' in p:out.add('Сохранённые Decompiled: Current/DefaultTool/Systray')
  if 'research-gm6ir0c' in p:out.add('GM6: recovered active')
 return ' / '.join(sorted(out)) or 'См. исходный файл'
helper=ROOT.parent.parent/'mechrevo/Decompiled/GCUService_Reference_w568w/GCUService/MyECIO/MyEcCtrl.cs'
custom=helper.parent.parent/'MyControlCenter/CustomizeCtrl.cs'
extension=helper.parent.parent/'Utility/ProjectIdExtensions.cs'
ext=extension.read_text(encoding='utf-8-sig')
biomaps=collections.defaultdict(list)
for entry in inv['files']:
 p=Path(entry['path'])
 if 'MyEcCtrl' not in p.name and p.name!='GCUService.decompiled.cs':continue
 text=Path('\\\\?\\'+str(p)).read_text(encoding='utf-8-sig',errors='replace')
 for call in re.finditer(r'(?:public|private|internal)\s+(?:static\s+)?BIOS_PROJECT_ID\s+GetBiosProjctID\([^)]*\)\s*\{',text):
  start=call.end();depth=1;end=start
  while end<len(text) and depth:
   depth += (text[end]=='{')-(text[end]=='}');end+=1
  body=text[start:end]
  for m in re.finditer(r'case\s+(\d+)\s*:\s*\w+\s*=\s*BIOS_PROJECT_ID\.(\w+)',body):
   biomaps[m[2]].append({'raw_case':int(m[1]),'path':str(p),'line':text.count('\n',0,start+m.start())+1,'scope':'GetBiosProjctID switch; register/mask must be checked in the same implementation'})
(HERE/'bios-project-switches.json').write_text(json.dumps(dict(biomaps),ensure_ascii=False,indent=2),encoding='utf-8')
groups={}
for m in re.finditer(r'List<ProjectID>\s+(\w+)\s*=.*?\{([^}]+)\}',ext,re.S):groups[m[1]]=set(re.findall(r'ProjectID\.(\w+)',m[2]))
rules={
5889:'740=23; 456.7=0; 771.0=0; 7D3.0=0',5890:'740=23; 456.7=0; 771.0=0; 7D3.0=1; 772!=3',
5895:'740=23; 456.7=0; 771.0=0; 7D3.0=1; 772=3',5891:'740=23; 456.7=1; 771.0=0',
5893:'740=23; 456.7=0; 771.0=1; 7D3.0=0',5894:'740=23; 456.7=1; 771.0=1; (7D3&F0)=30/70/A0',
5892:'740=23; 456.7=1; 771.0=1; (7D3&F0) не 30/70/A0',
6145:'740=24; 456.7=0; 771.0=0; 7D3.0=0',6146:'740=24; 456.7=0; 771.0=0; 7D3.0=1',
6147:'740=24; 456.7=1; 771.0=0; 772=1',6154:'740=24; 456.7=1; 771.0=0; 772=4',
6148:'740=24; 456.7=1; 771.0=0; 772 не 1/4',6149:'740=24; 456.7=0; 771.0=1',
6150:'740=24; 456.7=1; 771.0=1; (7D2&1F)=8',
6151:'740=24; 456.7=1; 771.0=1; (7D2&1F)=16/21/17; адаптер!=150W',
6155:'740=24; 456.7=1; 771.0=1; (7D2&1F)=16/21/17; адаптер=150W',
6152:'740=24; 456.7=1; 771.0=1; (7D2&1F)=12; адаптер!=150W',
6156:'740=24; 456.7=1; 771.0=1; (7D2&1F)=12; адаптер=150W',
6153:'740=24; 456.7=1; 771.0=1; (7D2&1F)=18; адаптер!=150W',
6157:'740=24; 456.7=1; 771.0=1; (7D2&1F)=18; адаптер=150W'}
# Actual known notebooks first. The old characteristics file is a report index,
# not evidence that every board appears literally in every CC binary.
old=ROOT.parent/'Ноутбуки — характеристики.csv'
profiles=json.loads((CAT/'charge-project-coverage.json').read_text(encoding='utf-8'))['profiles']
actual={}
for cells in csv.reader(old.open(encoding='utf-8-sig',newline='')):
 if len(cells)!=12 or not cells[0].strip().startswith(('MECHREVO ','Honor ','MACHENIKE ','Eluktronics ')):continue
 cells=[x.strip() for x in cells];name=cells[0];board=name.split(' / ')[-1]
 matches=[p for p in profiles if p['board'].lower() in name.lower()]
 if matches:board=max(matches,key=lambda p:len(p['board']))['board']
 r=row(name,'Конкретный ноутбук: отчёт / дамп',**dict(zip(['Процессор','Видеокарта','BIOS: известные версии','EC: сообщённые версии'],cells[1:5])))
 r['Плата / модель SMBIOS']=board;r['Источники (файл:строка)']=str(old)+'; '+(matches[0]['profile_file'] if matches else str(ROOT/'src/gui/common/DeviceHeader.h'))
 r['Механизм определения / подключения']='Точный SMBIOS; OEM-возможности должны проверяться по собственной версии NVRAM/EC, а не по названию CPU'
 r['Control Center / корпус']='Модель известна из сохранённых пользовательских отчётов; буквальное имя в CC не установлено'
 r['Что не подтверждено']='Совпадение с шаблоном семейства не доказывает совместимость firmware или управления'
 if 'GM6IR0C' in name:
  r.update({'Control Center / корпус':'GM6: OEM active GCUService + recovered; CustomizeTarget 56','Project ID EC [740]':'25 (0x19), наблюдение дампа','Платформа EC [7C3]':'7: ранний дамп и OemHooksPei исходного BIOS','ROM ID / условия выбора':'Раннее наблюдение: 770=FF; не название прошивки','EC: имеющиеся образы':'MRO09 EC1.11; MRO10 EC1.12 (семейные payload, не аттестация установленного EC)','Механизм определения / подключения':'CustomizeCtrl.Init -> CustomizeTarget + GetProjectIdFromEC(740) -> GetProject2ExID; NVRAM/capability guards; RamFan1.5','Возможности из кода СС':'Office/Gaming/Turbo/Custom; RamFan1.5; EC SingleZone; NVRAM capability flags'})
 elif 'WUJIE' in name:
  r.update({'Control Center / корпус':'Сохранённый CC5.57.1.2 и GCUService reference (границы версий раздельны)','Project ID EC [740]':'26 (0x1A), наблюдение дампа','ROM ID / условия выбора':'Менялся приложением; заводское значение не устанавливается по позднему дампу','EC: имеющиеся образы':'BIOS N.1.20MRO15: EC core128 КиБ + PD; публикация версии1.18 прослежена в отдельном исследовании','Возможности из кода СС':'Клавиатура EC SingleZone; прочие функции определяются NVRAM и точным backend'})
 elif 'HYDROC' in name:
  r.update({'Control Center / корпус':'Eluktronics CC5.23.51.56; recovered readable/raw C#','Платформа EC [7C3]':'13 (0x0D), наблюдение сохранённого дампа','Возможности из кода СС':'RamFan1.5; HID keyboard/base/logo/hinge capabilities; LPP BLE PumpControl/FanControl; наличие зон проверяется по HID'})
 elif 'GM5HG0A' in name:r.update({'Control Center / корпус':'GM5 OEM CC5.52.49.6: GCUService / DefaultTool / Systray','Возможности из кода СС':'Uniwill NVRAM; RamFan1.5; AMD CPU PL; keyboard capabilities'})
 elif 'MACHENIKE' in name:r['Control Center / корпус']='L16 recovered/decompiled; защищённые тела не считаются восстановленными'
 elif 'Honor' in name:r.update({'Control Center / корпус':'Honor/Huawei PC Manager: отдельный HWMI, не Uniwill','Механизм определения / подключения':'SMBIOS HLYL-WXX9-PCB + Huawei HWMI; ProjectID740/7C3 Uniwill неприменимы','Возможности из кода СС':'Собственный Huawei HWMI; не переносить Uniwill адреса','EC: имеющиеся образы':'Отдельная Honor EC identity; BIOS Insyde H2O, не AMI'})
 if 'GM6IR0C' in name:r['Источники (файл:строка)']+='; '+str(CAT/'source-units.json')+'; '+str(CAT/'bios-platform-initialization-index.csv')
 if 'WUJIE' in name:r['Источники (файл:строка)']+='; '+str(ROOT/'research/wujie-full-bios-20261024/EVIDENCE.json')+'; '+str(ROOT/'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin')
 if 'HYDROC' in name:r['Источники (файл:строка)']+='; '+str(ROOT/'research/devices/hydroc-16-g2/device-review-20261019.txt')
 actual[board]=r
# Every named OEM ProjectID is retained. None is an absence sentinel, not a model.
for name,items in inv['project_enums'].items():
 if name=='None':continue
 values=sorted(set(x['value'] for x in items));v=values[0]
 raw=23 if 5889<=v<=5895 else 24 if 6145<=v<=6157 else v
 features=[key for key,valueset in groups.items() if name in valueset]
 r=row(name,'Именованный проект / семейство СС',**{'Control Center / корпус':packages(items),'Project ID EC [740]':str(raw)+f' (0x{raw:02X})','Уточнённый ProjectID СС':' / '.join(map(str,values)),'ROM ID / условия выбора':rules.get(v,'Нет дополнительного условия в этом enum; enum не задаёт OEM-платформу 7C3'),'Механизм определения / подключения':'CustomizeCtrl.Init -> GetProjectIdFromEC -> GetProject2ExID; точные условия reference_w568w: '+rules.get(v,'прямой именованный ProjectID'),'Возможности из кода СС':'; '.join(features) or 'Именование ProjectID; конкретные функции требуют отдельной проверки','Что не подтверждено':'Торговое имя, точный SMBIOS, версия EC и значение7C3 не следуют из enum. Условия reference не автоматически применимы ко всем сборкам','Источники (файл:строка)':'; '.join(at(x) for x in items[:3])+'; '+str(helper)+':597; '+str(custom)+':26; '+str(extension)+':57'})
 # Enum values >255 are computed software IDs, never raw byte740.
 if v>255:r['Уточнённый ProjectID СС']+=f' (0x{v:04X}, вычисляется в СС)'
# BIOS project symbols have their own namespace. Do not merge IDP with raw
# ProjectID.IDP merely because both names are spelled alike.
for name,items in inv['bios_project_enums'].items():
 if name=='NA':continue
 mapping=biomaps.get(name,[])
 rawvals=sorted(set(x['raw_case'] for x in mapping))
 row('BIOS project '+name,'Семейство BIOS: конкретный ноутбук не установлен',**{'Control Center / корпус':packages(items),'BIOS_PROJECT_ID (другой справочник)':' / '.join(f"{v} (0x{v:02X})" for v in sorted(set(x['value'] for x in items)))+' (значения enum, не сырые байтыEC)','Механизм определения / подключения':'GetBiosProjctID; отдельный тип Define.BIOS_PROJECT_ID, не адрес740 и не7C3; '+('в recovered switch raw case='+','.join(map(str,rawvals))+'; reference_w568w читает74C с маской0F/3F; маска и регистр каждой другой версии требуют собственной проверки' if mapping else 'сырой selector не восстановлен'),'Что не подтверждено':'Связь с конкретной платой/ProjectID/версиейEC не установлена; символ не означает отдельную подтверждённую модель','Источники (файл:строка)':'; '.join(at(x) for x in items[:3])+('; '+at(mapping[0]) if mapping else '')})
# Saved firmware families: group all revisions/variants in one row per family.
units=json.loads((CAT/'source-units.json').read_text(encoding='utf-8'))
byfamily=collections.defaultdict(list)
for u in units:
 if u['kind']!='EC':continue
 if u['source_label'].startswith('EC-MRO'):continue # already grouped under exact GM6
 family=re.split(r'N\d',u['source_label'])[0]
 byfamily[family].append(u)
init=list(csv.DictReader((CAT/'bios-platform-initialization-index.csv').open(encoding='utf-8-sig')))
for family,uu in sorted(byfamily.items()):
 bios=sorted(set(re.sub(r'N(\d)(\d{2})(MRO\d+)',r'N.\1.\2\3',re.search(r'N\d\d{2}MRO\d+',u['source_label']).group(0)) for u in uu))
 stores=sorted(set(x['value'] for x in init if any(x['source']==u['source_label'].split(' [')[0] for u in uu)))
 row(family,'Семейство из имени OEM-прошивки; точная плата не установлена',**{'Плата / модель SMBIOS':family+' (маска файла, не прочитанный SMBIOS)','BIOS: известные версии':'; '.join(bios)+' (из имени пакета)','EC: имеющиеся образы':'; '.join(f"{u['source_label']}: {u['bytes']} байт; SHA256={u['sha256']}" for u in uu),'Платформа EC [7C3]':'; '.join(stores)+': selected OemHooksPei store; не аттестация runtime' if stores else N,'Механизм определения / подключения':'Семейство CAP + точный SHA payload; конкретный аппаратный вариант должен определять сам OEM-код','Что не подтверждено':'Точная модель, установленная версияEC и выбор варианта. Строка ITE EC-V14.6 в заголовке не подменяет версиюEC ноутбука','Источники (файл:строка)':'; '.join(u['path'] for u in uu)+'; '+str(CAT/'source-units.json')+'; '+str(CAT/'bios-platform-initialization-index.csv')})
assert len({r['Ноутбук / семейство'] for r in rows})==len(rows)
assert len(actual)==16,(len(actual),list(actual))
assert not inv['read_errors'],inv['read_errors'][:2]
for r in rows:
 assert set(r)==set(headers)
 for k,v in r.items():r[k]=re.sub(r'[\r\n]+',' ',str(v)).strip()
payload={'headers':headers,'rows':rows,'counts':dict(collections.Counter(x['Вид записи'] for x in rows)),'source_unique_files':inv['unique_source_files'],'source_errors':len(inv['read_errors']),'scope':'Local explicit named ProjectID and BIOS_PROJECT_ID enums, known reported boards, saved canonical EC firmware families. Not a complete semantic recovery of all protected/native/archive-hidden CC identities. One record per named model/family; multiple variants are cells, not duplicate rows.'}
(HERE/'catalogue-rows.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'rows':len(rows),'counts':payload['counts'],'unique_sources':inv['unique_source_files']},ensure_ascii=False))
