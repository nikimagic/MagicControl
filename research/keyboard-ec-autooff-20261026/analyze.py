"""Decode saved EC images only; no live hardware access."""
from pathlib import Path
import hashlib,json,sys
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'build/honor-analysis'))
from disasm51 import addresses,codeanalyzer,instructions
census=json.loads((ROOT/'research/battery-logger-comparison-20261026/census.json').read_text(encoding='utf8'))
specs={'WUJIE':[(2,0xbb34,0xbbf0),(2,0xb5c0,0xb710),(2,0xc000,0xc0d0),(2,0xd626,0xd644),(2,0xd7fa,0xd805),(2,0xdc00,0xdc14),(2,0xdc80,0xdcb0),(2,0xdd0e,0xdd16),(0,0x6fe4,0x6ffa),(2,0x86a0,0x8708),(0,0x2630,0x2680),(2,0xe450,0xe4a8),(2,0xb47b,0xb4c0),(2,0xb7bf,0xb800)],
       'GM6_1.12':[(2,0xb529,0xb5e5),(2,0xb080,0xb1c0),(2,0xba40,0xbad0),(2,0xd675,0xd687),(2,0xd827,0xd832),(2,0xdad9,0xdae8),(2,0xda8d,0xda95),(2,0xdc50,0xdc80),(2,0x86b0,0x8710),(0,0x8f80,0x8fc0),(0,0x3400,0x3450),(2,0xe4b0,0xe500),(2,0xafb3,0xaff0),(2,0xb253,0xb280)]}
out=[];proof=[]
specs['WUJIE'] += [(0,0xdf0,0xe09),(0,0xe09,0xea5),(0,0x1594,0x15a6),(0,0x533,0x559),(2,0xd8d9,0xd8e8)]
specs['GM6_1.11']=[(2,0xb521,0xb5bd),(2,0xd66d,0xd674),(2,0xd81f,0xd826),(2,0xdc5a,0xdc61),(2,0xda85,0xda8d)]
for item in census['firmware']:
    if item['label'] not in specs:continue
    data=Path(item['path']).read_bytes();sha=hashlib.sha256(data).hexdigest();assert sha==item['sha256']
    sites=[]
    rawhits=[]
    timer={'WUJIE':0xbb34,'GM6_1.11':0xb521,'GM6_1.12':0xb529}[item['label']]
    for name,needle in [('timer_DPTR',b'\x90'+timer.to_bytes(2,'big')),('timer_LCALL',b'\x12'+timer.to_bytes(2,'big')),('activity_bit23',b'\xd2\x23'),('activity_bit24',b'\xd2\x24')]:
        at=0
        while True:
            at=data.find(needle,at)
            if at<0:break
            rawhits.append({'name':name,'file_offset':hex(at)});at+=1
    for bank in range(3):
        rom=data[:0x8000]+data[(bank+1)*0x8000:(bank+2)*0x8000]
        decode=codeanalyzer.CodeAnalyzer(instructions.Instructions(),rom,addresses.Addresses(),0,True,[],set())._CodeAnalyzer__disassemble_instruction
        lo=0 if bank==0 else 0x8000
        for target in {'WUJIE':[0xdc9a,0xc76d,0xd667],'GM6_1.11':[0xdc5a,0xba9b,0xd70f],'GM6_1.12':[0xdc62,0xbaa3,0xd717]}[item['label']]:
            needle=b'\x12'+target.to_bytes(2,'big');at=lo
            while True:
                at=rom.find(needle,at)
                if at<0:break
                sites.append({'bank':bank,'CODE':hex(at),'candidate_LCALL':hex(target)});at+=1
        for b,start,end in specs[item['label']]:
            if b!=bank:continue
            pc=start;lines=[f"{item['label']} bank{bank} CODE {start:04X}..{end:04X}"]
            while pc<end:
                text,n=decode(pc);lines.append(f"{pc:04X} {rom[pc:pc+n].hex():<10} {text.strip()}");pc+=n
            out.append('\n'.join(lines))
    proof.append({'label':item['label'],'path':item['path'],'sha256':sha,'candidate_calls':sites,'raw_hits':rawhits,
                  'scope':'Bounded decodes and raw call candidates; no complete CFG or execution proof.'})
(OUT/'WINDOWS.asm').write_text('\n\n'.join(out)+'\n',encoding='utf8')
(OUT/'EVIDENCE.json').write_text(json.dumps(proof,ensure_ascii=False,indent=2),encoding='utf8')
for row in proof:print(row['label'],row['raw_hits'])
