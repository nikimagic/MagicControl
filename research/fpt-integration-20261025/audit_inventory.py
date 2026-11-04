"""Read saved user inventories only, without any hardware/tool execution."""
from pathlib import Path
import hashlib,json,re,zipfile
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
rows=[]
patterns=['X5SP4NAG','HLYL','GM6IR0C','GM5HG0A','HYDROC','GM6PX7','X6xx55xW','X6xR55xY','GM6BG0','X6xx57TW','X6DR55','GX6HG0','X8xR55','X6AR55','GM6IX9','L16']
def walk(value):
    if isinstance(value,str):yield value
    elif isinstance(value,dict):
        for v in value.values():yield from walk(v)
    elif isinstance(value,list):
        for v in value:yield from walk(v)
def inspect(label,data):
    try:obj=json.loads(data.decode('utf-8-sig'))
    except (ValueError,UnicodeError):return
    text='\n'.join(walk(obj));models=[x for x in patterns if x.casefold() in text.casefold()]
    if not models:return
    ids=sorted(set(re.findall(r'VEN_8086&DEV_([A-Fa-f0-9]{4})',text)))
    lpc=sorted(set(re.findall(r'VEN_8086&DEV_([A-Fa-f0-9]{4})[^\n|]{0,90}&CC_0601',text)))
    cpu=sorted(set(x.strip() for x in walk(obj) if re.search(r'Intel.*(?:Core|Ultra)|AMD Ryzen|Ryzen [3579]',x,re.I) and len(x)<170))
    bios=[]
    def named(value):
        if isinstance(value,dict):
            if 'name' in value and 'value' in value and re.search(r'BIOS|ROM.*Size',str(value['name']),re.I):bios.append({str(value['name']):value['value']})
            for v in value.values():named(v)
        elif isinstance(value,list):
            for v in value:named(v)
    named(obj)
    rows.append({'source':label,'sha256':hashlib.sha256(data).hexdigest(),'models':models,'cpu_strings':cpu[:12],
                 'intel_lpc_ids':lpc,'intel_pci_ids':ids,'bios_properties':bios[:18]})
for path in sorted((ROOT/'Users_logs').glob('*.zip')):
    if path.stat().st_size>60*1024*1024 or path.name in ['backup.zip','new.zip']:continue
    try:
        with zipfile.ZipFile(path) as z:
            for entry in z.infolist():
                if entry.file_size<8*1024*1024 and re.search(r'(inventory|board|precheck|bios-inventory)\.json$',entry.filename,re.I):
                    inspect(path.name+'::'+entry.filename,z.read(entry))
    except zipfile.BadZipFile:pass
for path in [ROOT/'Users_logs/inventory.json',ROOT/'MagicControl/windows/bios-inventory.json']:
    if path.exists():inspect(str(path.relative_to(ROOT)),path.read_bytes())
(OUT/'inventory-evidence.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
summary={}
for row in rows:
    for model in row['models']:
        value=summary.setdefault(model,{'cpu':set(),'lpc':set(),'intel_ids':set(),'sources':0})
        value['cpu'].update(row['cpu_strings']);value['lpc'].update(row['intel_lpc_ids']);value['intel_ids'].update(row['intel_pci_ids']);value['sources']+=1
compact={k:{f:sorted(v) if isinstance(v,set) else v for f,v in row.items()} for k,row in summary.items()}
(OUT/'inventory-summary.json').write_text(json.dumps(compact,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(compact,ensure_ascii=False,indent=2))
