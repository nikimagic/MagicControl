"""Join static sources, original reports and valid RAM snapshots without writes."""
from pathlib import Path
import json,re,hashlib,collections,sys,csv
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];CAT=ROOT/'research/ec-platform-catalogue-20261023';BASE=ROOT/'research/cc-laptop-catalogue-20261026'
obs=json.loads((HERE/'observations.json').read_text(encoding='utf8'));fw=json.loads((HERE/'firmware-links.json').read_text(encoding='utf8'))
base=json.loads((BASE/'catalogue-rows.json').read_text(encoding='utf8'))
rows=base['rows'];headers=base['headers']+['Сопоставленные платы','Основание сопоставления','Готовность подключения','Каких данных не хватает']
sha=lambda raw:hashlib.sha256(raw).hexdigest()
def table(raw):
 result={};pos=0
 while pos+4<=len(raw):
  typ,n=raw[pos],raw[pos+1]
  if n<4 or pos+n>len(raw):break
  end=raw.find(b'\0\0',pos+n)
  if end<0:break
  strings=raw[pos+n:end].split(b'\0')
  def s(off):
   v=raw[pos+off] if off<n else 0
   return strings[v-1].decode('ascii',errors='replace') if 0<v<=len(strings) else None
  if typ==0:result.update(bios_vendor=s(4),bios=s(5))
  elif typ==1:result.update(manufacturer=s(4),model=s(5))
  elif typ==2:result.update(board=s(5))
  pos=end+2
 return result
templates=[]
for r in fw['section_hits']:
 if 'AB56DC60' not in r['section']:continue
 raw=Path(r['section']).read_bytes();assert sha(raw)==r['sha256']
 t=table(raw);assert t.get('board')
 templates.append(dict(r,identity=t,scope='SMBIOS Types0/1/2 decoded from saved firmware default table; not a live machine'))
# Fixed version publisher: CLR A; MOV DPTR,table; MOVC A,@A+DPTR;
# MOV DPTR,0454; MOVX @DPTR,A. Preserve the raw byte, not a guessed version.
for u in fw['units']:
 if u['kind']!='EC':continue
 raw=Path(u['path']).read_bytes();publish=[]
 for m in re.finditer(rb'\xe4\x90(..)\x93\x90\x04\x54\xf0',raw,re.S):
  bank=m.start()//0x8000-1;addr=int.from_bytes(m[1],'big');offset=addr if addr<0x8000 else bank*0x8000+addr
  if bank not in (0,1,2) or offset>=len(raw):continue
  publish.append(dict(code_store_file_offset=m.start(),bank=bank,table_CODE=addr,table_file_offset=offset,raw_454=raw[offset],scope='Exact selected bank mapping and MOVC/MOVX byte sequence; full startup reachability/SMBIOS version conversion not asserted'))
 u['version_publishers']=publish
# Independent CIL evidence from the exact service present in GM6PX7X's report.
data=ROOT/'build/nested-oem-references/7578-recovered-data'
recovery=json.loads((data/'analysis-assembly-recovery.json').read_text(encoding='utf8'))
assert sha(Path(recovery['original']).read_bytes())==recovery['original_sha256']
assert sha((data/recovery['analysis_copy']).read_bytes())==recovery['analysis_sha256']
sys.path.insert(0,str(CAT));from cil_data_reader import decode_cil
meta=json.loads((data/'metadata.json').read_text(encoding='utf8'));names=json.loads((data/'token-names.json').read_text(encoding='utf8'))
ops={x['code']:x for x in json.loads((ROOT/'research/devices/gm6ir0c/battery-all-modes-20261021/oem-audit/cil-opcodes.json').read_text(encoding='utf8'))}
cil=[]
for token in (0x0600062c,0x06000641,0x0600064b,0x0600064c):
 m=next(m for m in meta['methods']if m['token']==token)
 instructions=decode_cil(bytes.fromhex(m['il']),ops,names)
 cil.append(dict(m,instructions=instructions))
for token,addr in ((0x0600062c,0x740),(0x06000641,0x78e),(0x0600064b,0x7ca),(0x0600064c,0x74c)):
 m=next(x for x in cil if x['token']==token)
 assert any(i['opcode']=='ldc.i4' and i['operand']==addr for i in m['instructions'])
sources=[]
for label,p in [('CC5.57.1.2',CAT/'managed-provenance/GCUService-readable-strings.cs'),('L16 CC5.60.60.17',CAT/'managed-l16-5.60.60.17/GCUService-readable-strings.cs'),('HYDROC CC5.23.51.56',ROOT/'research/devices/hydroc-16-g2/oem-5.23.51.56/recovered-20261019/readable-csharp/020000FC-MyECIO.MyEcCtrl.cs')]:
 text=p.read_text(encoding='utf-8-sig');methods=[]
 for method in ('GetProjectIdFromEC','IsSupportRamFan1p5','IsSupport6BitID','GetBiosProjctID'):
  m=re.search(r'public\s+\w+\s+'+method+r'\([^)]*\)\s*\{',text)
  if not m:continue
  end=m.end();depth=1
  while end<len(text) and depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
  methods.append(dict(name=method,line=text.count('\n',0,m.start())+1,body=text[m.start():end]))
 sources.append(dict(label=label,file=str(p),sha256=sha(p.read_bytes()),methods=methods))
modern=next(x for x in sources if x['label']=='L16 CC5.60.60.17')
switch=next(x['body']for x in modern['methods']if x['name']=='GetBiosProjctID')
codec={int(a):b for a,b in re.findall(r'case\s+(\d+)\s*:\s*result\s*=\s*BIOS_PROJECT_ID\.(\w+)',switch)}
byboard=collections.defaultdict(list);byreport=collections.defaultdict(list)
for d in obs['dumps']:byboard[d['identity']['board']].append(d)
for r in obs['reports']:byreport[r['board']].append(r)
boardmaps={}
for board,dd in byboard.items():
 vals=sorted(set(d['bytes']['740']for d in dd if d['bytes']['740'] is not None))
 dec=[]
 for d in dd:
  b=d['bytes'];mask=0x3f if b['7CA'] is not None and b['7CA']&1 else 0x0f
  if b['7CA'] is None or b['74C'] is None:continue
  raw=b['74C']&mask;name=codec.get(raw)
  dec.append({'raw_74C':b['74C'],'7CA':b['7CA'],'mask':mask,'selector':raw,'name':name,'source':d['source']})
 boardmaps[board]={'project_740':vals,'BIOS_codec':dec,'platform_7C3':sorted({d['bytes']['7C3']for d in dd if d['bytes']['7C3'] is not None}),'ROM_770':sorted({d['bytes']['770']for d in dd if d['bytes']['770'] is not None}),'RamFan1p5_bit_78E_6':sorted({bool(d['bytes']['78E']&0x40) for d in dd if d['bytes']['78E'] is not None}),'snapshot_occurrences':len(dd),'unique_snapshots':len({d['sha256']for d in dd}),'evidence':[d['source']for d in dd]}
def bname(name):
 for b in sorted(set(byboard)|set(byreport),key=len,reverse=True):
  if b in name:return b
 return None
newfields=headers[-4:]
for r in rows:
 r.update(dict.fromkeys(newfields,'Не установлено'))
 if r['Вид записи'].startswith('Конкретный ноутбук'):
  board=bname(r['Ноутбук / семейство']);r['Сопоставленные платы']=board or r['Плата / модель SMBIOS']
  if board in byreport:
   reports=byreport[board];projects=sorted({x['registry_ProjectID']for x in reports if x['registry_ProjectID'] is not None})
   r['Уточнённый ProjectID СС']='/'.join(map(str,projects))+' (снимок реестра OEM, не прямое чтение740)'
   versions=sorted({x['CC_version'] for x in reports if x['CC_version']})
   r['Control Center / корпус']+='; в отчётах: '+('/'.join(versions) or 'AppVersion отсутствует')
   r['Основание сопоставления']='Точный SMBIOS + реестр установленного OEM; SHA GCUService из исходного отчёта'
   r['Источники (файл:строка)']+='; '+reports[0]['source']
  if board in boardmaps:
   b=boardmaps[board];r['Project ID EC [740]']='/'.join(map(str,b['project_740']))+' (сырые валидные дампы)'
   types=sorted({x['name']for x in b['BIOS_codec']if x['name']})
   r['BIOS_PROJECT_ID (другой справочник)']='/'.join(types)+'; 74C&3F при7CA.0=1 (codec СС, не OEM-платформа)'
   r['Платформа EC [7C3]']='/'.join(map(str,b['platform_7C3']))+' (наблюдалось в дампах; не утверждение о заводском значении)'
   r['ROM ID / условия выбора']='770='+ '/'.join(f'{v:02X}'for v in b['ROM_770'])+' в сохранённых сеансах; часть значений после наших изменений'
   r['Основание сопоставления']='Точный SMBIOS в дампе/его отчёте + valid740/74C/7CA/7C3; декодирование по версии СС'
   r['Готовность подключения']='Уже есть отдельный backend; сохранить текущие guards. Аппаратные функции оцениваются отдельно'
   r['Каких данных не хватает']='Для новых функций: собственные capability/NVRAM/HID; ни ID, ни platform не разрешают универсальные записи'
   r['Источники (файл:строка)']+='; '+b['evidence'][0]
  elif 'Honor' in r['Ноутбук / семейство']:
   r['Готовность подключения']='Уже есть отдельный Huawei HWMI; Uniwill740/7C3 неприменимы'
   r['Каких данных не хватает']='Новые функции подтверждать по Huawei/Honor протоколу'
  else:
   r['Project ID EC [740]']='Нет прямого EC-дампа в рассмотренных исходниках; число реестра СС записано отдельно'
   r['Готовность подключения']='Кандидат на отдельный профиль/чтение; управление EC пока не включать'
   r['Каких данных не хватает']='Согласованный EC+valid, собственная ACPI/DSDT, NVRAM capabilities, HID descriptors, точная версия/хэш драйвера'
   if board=='GM6PX7X':
    r['Готовность подключения']='Первый кандидат на диагностический backend: точный GCUService7578 имеется и связан с отчётом; до управления нужен EC-дамп'
    r['Основание сопоставления']+='; SHA7578CB27 совпал с разобранным оригиналом; CIL740/78E.6/7CA.0/74C перепроверен'
    r['Источники (файл:строка)']+='; '+str(data/'metadata.json')
  r['Механизм определения / подключения']+='; далее capability route FANs: RamFan1.5, commercial classification, CustomizeTarget; lighting: NVRAM + реальный HID'
# Bind BIOS family symbols to observed boards. This does not equate each symbol
# to a unique physical notebook; many boards can share one switch branch.
for r in rows:
 name=r['Ноутбук / семейство']
 if name.startswith('BIOS project '):
  family=name.removeprefix('BIOS project ')
  boards=sorted(b for b,v in boardmaps.items()if any(x['name']==family for x in v['BIOS_codec']))
  if boards:
   r['Сопоставленные платы']=' / '.join(boards)
   r['Основание сопоставления']='Валидный74C и7CA в дампах; exact switch codec из сохранённого СС. Не уникальная торговая модель'
   r['Готовность подключения']='Связано с существующим backend; перечисленные платы не исчерпывают семейство'
   r['Каких данных не хватает']='Остальные платы семейства без собственного SMBIOS/EC не определены'
   r['Механизм определения / подключения']='7CA.0 -> маска0F/3F; EC74C&mask -> BIOS_PROJECT_ID; исходник '+modern['file']
 if name=='GK7NXXR':
  r['Сопоставленные платы']='GM6PX7X / GM6BG0X: только совпадение числа ProjectID16 в OEM-реестре'
  r['Основание сопоставления']='Имя старого enum не является доказанным SMBIOS этих современных моделей'
  r['Готовность подключения']='Не создавать алиас GK7NXXR -> GM6PX7X/GM6BG0X без дополнительных доказательств'
  r['Каких данных не хватает']='Точная модельная ветка и валидные данные740/74C/7CA/78E'
# Decode full SMBIOS templates instead of matching short substrings such as L16.
for t in templates:
 src=t['container_source'];family=re.split(r'N\d',src)[0]
 r=next((x for x in rows if x['Ноутбук / семейство']==family),None)
 if not r:continue
 identity=t['identity'];board=identity['board'];known=board in byreport or board in byboard
 r['Плата / модель SMBIOS']=board+' (шаблон BIOS, не live SMBIOS)'
 r['Сопоставленные платы']=board
 ecunits=[u for u in fw['units'] if u['kind']=='EC' and u['source'].split(' [')[0]==src]
 projects=sorted({p['value']for u in ecunits for p in u['literal_write_patterns']if p['address']=='740'})
 if projects:r['Project ID EC [740]']='/'.join(map(str,projects))+' (буквальная последовательность записи в сохранённом EC, не живое чтение)'
 publishers=sorted({p['raw_454']for u in ecunits for p in u['version_publishers']})
 if publishers:r['EC: имеющиеся образы']+='; выбранный MOVC/MOVX publisher454='+ '/'.join(f'{v:02X}'for v in publishers)+' (сырой байт; не подмена версииSMBIOS)'
 r['Основание сопоставления']='SMBIOS Types0/1/2 собственного decompressed freeform payload, SHA и offsets сохранены; '+('совпадает с точной платой в пользовательском отчёте' if known else 'пользовательского дампа этой платы нет')
 r['Механизм определения / подключения']='Точное имя платы из SMBIOS-шаблона + Project26/25 candidate bytes из EC + отдельный BIOS OemHooksPei store7; runtime guards не доказаны'
 r['Готовность подключения']='Дополнительный firmware-кандидат; подготовить отдельную диагностику, управление не включать'
 r['Каких данных не хватает']='EC+valid и ACPI конкретного устройства; проверить выбор шаблона, capability bits и точный driver; firmware image не является разрешением на запись'
 r['Источники (файл:строка)']+='; '+t['section']
 if not known:
  r['Ноутбук / семейство']='MECHREVO '+board
  r['Вид записи']='Плата из SMBIOS-шаблона BIOS; нет пользовательского дампа'
  r['Что не подтверждено']='Точная установленная конфигурация и совместимость runtime; шаблон не заменяет реальный ноутбук'
 # Attach matched firmware evidence to a known model without creating a duplicate
 # physical-model row. Family rows remain provenance references.
 if known:
  target=next((x for x in rows if x['Вид записи'].startswith('Конкретный ноутбук') and x['Сопоставленные платы']==board),None)
  if target:
   target['EC: имеющиеся образы']+='; '+src+' содержит SMBIOS-шаблон этой точной платы (другая ревизия не приравнивается к установленной)'
   target['Источники (файл:строка)']+='; '+t['section']
   if board not in boardmaps:target['Платформа EC [7C3]']='Живой EC неизвестен; store7 найден в OemHooksPei пакета '+src+' (не доказательство runtime/заводского значения установленной версии)'
for r in rows:
 if r['Вид записи'].startswith('Конкретный ноутбук') and any(s in r['Ноутбук / семейство'] for s in ('GM6IR0C','WUJIE')):
  date='18.10.2026 12:57:49' if 'GM6IR0C' in r['Ноутбук / семейство'] else '20.10.2026 20:05:17'
  r['Что не подтверждено']+='; первый отчёт '+date+' не содержит740/7C3/770: значения до вмешательства по нему неизвестны. UEFI firmware GUID не является EC ROM ID'
  r['Источники (файл:строка)']+='; '+str(HERE/'first-reports-provenance.json')
 for key,value in r.items():r[key]=re.sub(r'[\r\n]+',' ',str(value))
assert len(rows)==105 and len({r['Ноутбук / семейство']for r in rows})==105
assert len(templates)==6
assert {b:sorted({x['name']for x in m['BIOS_codec']})for b,m in boardmaps.items()}=={'L16':['ID6'],'HYDROC-16':['IDY'],'GM5HG0A':['IDA'],'GM6IR0C':['IDC'],'WUJIE Series-X5SP4NAG':['IDG']}
assert not obs['errors']
result=dict(headers=headers,rows=rows,counts=dict(collections.Counter(x['Вид записи']for x in rows)))
(HERE/'catalogue-rows.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
evidence=dict(scope='Offline static mapping; no writes, target code, driver, application or hardware executed',observed_boards=boardmaps,firmware_SMBIOS_templates=templates,EC_version_publishers=[x for x in fw['units']if x['kind']=='EC'],routing_sources=sources,GM6PX7X_exact_service=dict(original_sha256=recovery['original_sha256'],methods=cil),additional_template_boards=[t['identity']['board']for t in templates if t['identity']['board']not in byreport and t['identity']['board']not in byboard],limitations=['CC registry ProjectID is not a direct read of740','No universal meaning7C3 from CustomizeTarget/BIOS enum','ROM771/772 used in extended project logic are not ROM770','Templates contain placeholders and do not prove which variant boots','No old Project1..24 family uniquely bound to a modern board merely by a numeric enum match'])
(HERE/'mapping-evidence.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf8')
print('CSV rows',len(rows),'columns',len(headers),'observed BIOS families',[(b,sorted({x['name']for x in m['BIOS_codec']}))for b,m in boardmaps.items()],'additional firmware boards',evidence['additional_template_boards'])
