"""Offline exact-WUJIE EC bitfield inventory. No target-code execution."""
from pathlib import Path
import re,json,sys,hashlib,collections
OUT=Path(__file__).resolve().parent;ROOT=OUT.parents[1]
sys.path.insert(0,str(ROOT/'build/honor-analysis'))
from disasm51 import addresses,codeanalyzer,instructions
source=ROOT/'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin'
raw=source.read_bytes();digest=hashlib.sha256(raw).hexdigest()
assert len(raw)==0x20000 and digest=='5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8'
targets=[0x432,0x490,0x491,0x494,0x497,0x742,0x770,0x7a5,0x7a6,0x7b9,0x7c6,0x7c8,0x7cc,0x7d0,0x984]
decoders={}
for bank in range(3):
 rom=raw[:0x8000]+raw[(bank+1)*0x8000:(bank+2)*0x8000]
 decoders[bank]=(rom,codeanalyzer.CodeAnalyzer(instructions.Instructions(),rom,addresses.Addresses(),0,True,[],set())._CodeAnalyzer__disassemble_instruction)
sites=[];assembly=[]
for target in targets:
 for match in re.finditer(re.escape(b'\x90'+target.to_bytes(2,'big')),raw):
  physical=match.start();bank=None if physical<0x8000 else physical//0x8000-1;code=physical if bank is None else 0x8000+physical%0x8000
  rom,decode=decoders[0 if bank is None else bank];pc=code;rows=[]
  while pc<min(code+80,0x10000):
   try:decoded=decode(pc)
   except Exception:break
   if not decoded:break
   text,n=decoded;op=rom[pc:pc+n]
   if pc!=code and op[0]==0x90:break
   rows.append(dict(CODE=pc,file_offset=pc if pc<0x8000 else bank*0x8000+pc,hex=op.hex(),text=text.strip()))
   pc+=n
   if op[0]in (0x22,0x32,0x02,0x80):break
  site=dict(register=f'{target:03X}',bank=bank,CODE=code,file_offset=physical,instructions=rows,scope='Literal DPTR sequence and bounded linear decode; calls/branches may alter DPTR; not complete CFG proof')
  sites.append(site)
  assembly.append(f'\nREGISTER {target:03X} BANK {bank} CODE {code:04X} FILE {physical:05X}\n'+'\n'.join(f"{r['CODE']:04X} {r['hex']:<10} {r['text']}"for r in rows))
# Exact direct CALL/JMP byte candidates to small flag-getter helpers. Boundaries
# and bank-thunk effects remain a separate review, not silently inferred here.
for site in sites:
 code=site['CODE'];bank=site['bank'];callers=[]
 if bank is not None:
  begin=(bank+1)*0x8000;end=begin+0x8000
  for opcode in (0x12,0x02):
   for m in re.finditer(re.escape(bytes([opcode])+code.to_bytes(2,'big')),raw[begin:end]):
    offset=begin+m.start();callers.append(dict(file_offset=offset,CODE=0x8000+m.start(),bank=bank,kind='LCALL'if opcode==0x12 else'LJMP'))
 site['same_bank_literal_caller_candidates']=callers
report=dict(source=str(source),bytes=len(raw),sha256=digest,scope='Exact embedded WUJIE update EC core only. Read file data; no hardware/driver/firmware execution. Active whole-flash identity not attested.',targets=[f'{a:03X}'for a in targets],sites=sites,counts=dict(collections.Counter(s['register']for s in sites)),absent_literal_address_byteorders={'07D0':raw.count(bytes.fromhex('07d0')),'D007':raw.count(bytes.fromhex('d007'))})
(OUT/'literal-sites.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
(OUT/'LITERAL-WINDOWS.asm').write_text('\n'.join(assembly)+'\n',encoding='utf8')
print(json.dumps(report['counts']));print('total sites',len(sites))
