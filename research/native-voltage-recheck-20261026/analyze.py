"""Static saved-image/log review only. Does not open any hardware interface."""
from pathlib import Path
import hashlib,json,re,sys
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'../Decompile/HONOR_HLYL-WXX9/honor-analysis'));sys.dont_write_bytecode=True
from disasm51 import addresses,codeanalyzer,instructions
census=json.loads((ROOT/'research/battery-logger-comparison-20261026/census.json').read_text(encoding='utf8'))
images=[];listing=[]
specs={'WUJIE':[(2,0xd666,0xd675),(2,0xdd16,0xdd23),(2,0xd71a,0xd725),(2,0xd881,0xd88d),(2,0xc574,0xc7c9),(0,0x1ca8,0x1cb4),(1,0xe7c6,0xe9a8)],
       'GM6_1.11':[(2,0xc110,0xc365)],'GM6_1.12':[(2,0xc118,0xc36d)]}
for item in census['firmware']:
    data=Path(item['path']).read_bytes();sha=hashlib.sha256(data).hexdigest()
    assert sha==item['sha256']
    images.append({'label':item['label'],'path':item['path'],'sha256':sha,'bytes':len(data)})
    for bank,start,end in specs[item['label']]:
        rom=data[:0x8000]+data[(bank+1)*0x8000:(bank+2)*0x8000]
        decode=codeanalyzer.CodeAnalyzer(instructions.Instructions(),rom,addresses.Addresses(),0,True,[],set())._CodeAnalyzer__disassemble_instruction
        pc=start;lines=[f"{item['label']} bank{bank} CODE {start:04X}..{end:04X}"]
        while pc<end:
            text,n=decode(pc);lines.append(f"{pc:04X} {rom[pc:pc+n].hex():<10} {text.strip()}");pc+=n
        listing.append('\n'.join(lines))
(OUT/'FIRMWARE-WINDOWS.asm').write_text('\n\n'.join(listing)+'\n',encoding='utf8')
logs=[]
folder=ROOT/'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG'
for path in sorted(folder.glob('*.jsonl')):
    data=path.read_bytes();events=[];samples=[]
    for line in data.splitlines():
        try:row=json.loads(line)
        except (ValueError,UnicodeDecodeError):continue
        m=row.get('message','')
        if row.get('channel')=='battery' and ('voltage observation ' in m or 'voltage mode=' in m or 'native' in m.lower() and 'mode' in m.lower()):
            events.append({'utc':row.get('utc'),'message':m})
        if row.get('channel')=='battery_candidates' and 'SNAPSHOT ' in m:
            fields=dict(re.findall(r'(0x[0-9A-Fa-f]+)=0x([0-9A-Fa-f]+)/\d+',m))
            wanted={a:int(fields[a],16) for a in ['0x30E','0x522','0x7A6','0x741','0x490','0x4A6','0x7C3','0x770'] if a in fields}
            samples.append({'utc':row.get('utc'),'fields':wanted})
    # Preserve transitions, not thousands of repeated sampling records.
    transitions=[]
    for sample in samples:
        if not transitions or sample['fields']!=transitions[-1]['fields']:transitions.append(sample)
    logs.append({'path':str(path),'captured_bytes':len(data),'sha256_captured':hashlib.sha256(data).hexdigest(),
                 'scope':'Saved bytes captured during analysis; source may subsequently grow.',
                 'events':events,'transitions':transitions})
dumps=[]
for path in sorted((folder/'ec-dumps').glob('*/ec.bin'))[-5:]:
    data=path.read_bytes();vp=path.with_name('ec-valid.bin');valid=vp.read_bytes() if vp.exists() else b''
    fields={f'0x{a:03X}':{'raw':data[a] if a<len(data) else None,'valid':bool(valid[a]) if a<len(valid) else None}
            for a in [0x314,0x315,0x36c,0x36d,0x491,0x4a6,0x726,0x741,0x7a6,0x9c9,0x9ca]}
    dumps.append({'path':str(path),'sha256':hashlib.sha256(data).hexdigest(),'fields':fields})
(OUT/'EVIDENCE.json').write_text(json.dumps({'firmware':images,'logs':logs,'older_dumps':dumps},ensure_ascii=False,indent=2),encoding='utf8')
for log in logs:
    print(Path(log['path']).name,'bytes',log['captured_bytes'],'voltage_events',len(log['events']),'transitions',len(log['transitions']))
    if Path(log['path']).name=='battery-2.jsonl':
        for sample in log['transitions']:print(sample)
for dump in dumps:print(dump)
