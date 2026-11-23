"""Inspect original FPT19 file only. Never execute the imported PE or SYS."""
from pathlib import Path
import bisect,hashlib,json,re,struct,sys
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'research/firmware-reader-static-support'))
from runtime import new_run,publish_selection
out=new_run(root,'fpt19-inspection')
source=root/'src/platform/windows/runtime/IntelFpt19/FPTW64.exe'
b=source.read_bytes();sha=hashlib.sha256(b).hexdigest()
assert sha=='88491119862830235854cd29b9857de21870ac4dc16f74544b1a665f64d62807'
sys.path.insert(0,str(root.parent.parent/'mechrevo/.tools/pydeps'))
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_MEM,X86_REG_RIP,X86_OP_IMM
pe=struct.unpack_from('<I',b,60)[0];opt=pe+24;base=struct.unpack_from('<Q',b,opt+24)[0]
headers=opt+struct.unpack_from('<H',b,pe+20)[0];sections=[]
for n in range(struct.unpack_from('<H',b,pe+6)[0]):
    at=headers+n*40;size,rva,rs,ro=struct.unpack_from('<IIII',b,at+8)
    sections.append(dict(name=b[at:at+8].split(b'\0')[0].decode(),rva=rva,raw=ro,rawsize=rs))
def raw(rva):
    s=next(s for s in sections if s['rva']<=rva<s['rva']+s['rawsize']);return s['raw']+rva-s['rva']
def va(at):
    s=next(s for s in sections if s['raw']<=at<s['raw']+s['rawsize']);return base+s['rva']+at-s['raw']
er,es=struct.unpack_from('<II',b,opt+112+24);ea=raw(er)
functions=[struct.unpack_from('<III',b,at) for at in range(ea,ea+es,12)];starts=[base+f[0] for f in functions]
def owner(address):
    i=bisect.bisect_right(starts,address)-1
    return functions[i][0] if i>=0 and address<base+functions[i][1] else None
strings=[]
for m in re.finditer(rb'[\x20-\x7e]{5,}',b):
    text=m.group().decode()
    if not re.search(r'(?i)WNP|driver.*sys|DumpFlash|dump flash|memory dump|reading flash|Flash Descriptor|read access|read.access|Operation Passed|protection.*region|skipping.*region|region.*skip',text):continue
    try:address=va(m.start())
    except StopIteration:continue
    strings.append(dict(raw=m.start(),va=address,text=text,encoding='ascii'))
for m in re.finditer(rb'(?:[\x20-\x7e]\0){5,}',b):
    text=m.group().decode('utf-16-le')
    if not re.search(r'(?i)WNP|driver|\.sys',text):continue
    try:address=va(m.start())
    except StopIteration:continue
    strings.append(dict(raw=m.start(),va=address,text=text,encoding='utf16'))
byva={s['va']:s for s in strings};refs=[];cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True;cs.skipdata=True
for section in sections:
    if section['name']!='.text':continue
    for ins in cs.disasm(b[section['raw']:section['raw']+section['rawsize']],base+section['rva']):
        if not ins.id:continue
        for op in ins.operands:
            target=ins.address+ins.size+op.mem.disp if op.type==X86_OP_MEM and op.mem.base==X86_REG_RIP else op.imm if op.type==X86_OP_IMM else None
            if target in byva:refs.append(dict(va=hex(ins.address),function_rva=hex(owner(ins.address) or 0),instruction=ins.mnemonic+' '+ins.op_str,text=byva[target]['text']))
result=dict(sha256=sha,hardware_executed=False,strings=strings,refs=refs)
(out/'static-evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
selected=sorted(set(int(r['function_rva'],16) for r in refs if r['function_rva']!='0x0'))
(out/'selected-functions.txt').write_text(','.join(f'{x:x}' for x in selected)+',3c2b0,3c330,3c3ec\n')
publish_selection(out)
print(json.dumps(dict(output=str(out),strings=[s['text'] for s in strings if 'WNP' in s['text'] or '.sys' in s['text']],refs=refs),ensure_ascii=False,indent=2))
