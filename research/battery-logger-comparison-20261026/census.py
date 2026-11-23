"""Full static battery logger field census for WUJIE and GM6. No hardware IO."""
from pathlib import Path
import json,re,hashlib,sys,collections
OUT=Path(__file__).resolve().parent;ROOT=OUT.parents[1]
sys.path.insert(0,str(ROOT/'../Decompile/HONOR_HLYL-WXX9/honor-analysis'));sys.dont_write_bytecode=True
from disasm51 import addresses,codeanalyzer,instructions
sha=lambda b:hashlib.sha256(b).hexdigest()
sources={'WUJIE':ROOT/'src/protocols/uniwill/x5sp4nag/BatteryCandidates.h','GM6':ROOT/'src/protocols/uniwill/gm6ir0c/BatteryDecode.h','GM6_CURRENT_CALC':ROOT/'src/protocols/uniwill/gm6ir0c/BatteryCurrentDiagnostics.h'}
catalogs={};source_records=[]
for model,p in sources.items():
 text=p.read_text(encoding='utf-8-sig');source_records.append(dict(path=str(p),sha256=sha(p.read_bytes())))
 end=text.find('};',text.find('batteryCandidates[]{' if model!='GM6_CURRENT_CALC' else 'currentDiagnosticFields[]{'))
 text=text[:end]
 fields=[]
 for m in re.finditer(r'\{(0x[0-9A-Fa-f]+),(\d+),(true|false),"([^"]+)",(true|false)(?:,(true|false))?\}',text):
  fields.append(dict(address=int(m[1],16),width=int(m[2]),endian='BE'if m[3]=='true'else'LE',logger_label=m[4],logger_marked_confirmed=m[5]=='true',sampled_telemetry=m[6]=='true',source_line=text.count('\n',0,m.start())+1))
 catalogs[model]=fields
assert len(catalogs['WUJIE'])>50 and len(catalogs['GM6'])==27 and len(catalogs['GM6_CURRENT_CALC'])==14
definitions={'WUJIE':catalogs['WUJIE'],'GM6':catalogs['GM6']+catalogs['GM6_CURRENT_CALC']}
files={'WUJIE':ROOT/'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin','GM6_1.11':ROOT/'research/devices/gm6ir0c/battery-all-modes-20261021/firmware-sources/EC-MRO09-1.11.bin','GM6_1.12':ROOT/'research/devices/gm6ir0c/battery-all-modes-20261021/firmware-sources/EC-MRO10-1.12.bin'}
expected={'WUJIE':'5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8','GM6_1.11':'5059e6cb58addec21ad7fd5693f4c082230fbd8c7692fb07799c90adabae5c4c','GM6_1.12':'5659c8217e9d93d3fd6fe278756872d7c5adbd15a0687d35705e2df4bb5c05dd'}
firmware=[]
for label,p in files.items():
 raw=p.read_bytes();assert len(raw)==131072 and sha(raw)==expected[label]
 model='WUJIE'if label=='WUJIE'else'GM6';fields=definitions[model]
 # Include both bytes of word fields and control fields required for the chain.
 targets=sorted({f['address']+n for f in fields for n in range(f['width'])}|{0x740,0x741,0x742,0x770,0x7c3,0x78e,0x984,0x491,0x494,0x0832,0x0834,0x0836})
 decoders={}
 for bank in range(3):
  rom=raw[:0x8000]+raw[(bank+1)*0x8000:(bank+2)*0x8000]
  decoders[bank]=(rom,codeanalyzer.CodeAnalyzer(instructions.Instructions(),rom,addresses.Addresses(),0,True,[],set())._CodeAnalyzer__disassemble_instruction)
 sites=[]
 for target in targets:
  for match in re.finditer(re.escape(b'\x90'+target.to_bytes(2,'big')),raw):
   offset=match.start();bank=None if offset<0x8000 else offset//0x8000-1;code=offset if bank is None else 0x8000+offset%0x8000
   rom,decode=decoders[bank or 0];pc=code;window=[]
   while pc<min(code+48,0x10000):
    try:decoded=decode(pc)
    except Exception:break
    if not decoded:break
    text,n=decoded;op=rom[pc:pc+n]
    if pc!=code and op[0]==0x90:break
    window.append(dict(CODE=pc,hex=op.hex(),text=text.strip()));pc+=n
    if op[0]in (0x22,0x32,0x02,0x80):break
   sites.append(dict(address=target,bank=bank,CODE=code,file_offset=offset,instructions=window))
 counts=collections.Counter(x['address']for x in sites)
 coverage=[]
 for f in fields:
  coverage.append(dict(f,literal_DPTR_count_by_byte={f'{f["address"]+i:03X}':counts[f['address']+i]for i in range(f['width'])},interpretation='Unreviewed semantic role unless separately linked in semantic findings. A literal or a logger confirmed flag alone is not consumer proof.'))
 item=dict(label=label,model=model,path=str(p),sha256=sha(raw),bytes=len(raw),fields=coverage,sites=sites,limits='Bounded literal DPTR census, not exhaustive pointer/CFG coverage. No firmware/hardware execution. Embedded payload is not whole running-flash attestation.')
 firmware.append(item)
 (OUT/(label+'-literal-sites.json')).write_text(json.dumps(item,ensure_ascii=False,indent=2),encoding='utf8')
result=dict(scope='Entire current logger catalogues for both boards, plus GM6 CURRENT_CALC diagnostics. Windows metadata/calculated metrics catalogued separately; not assigned EC register identities by analogy.',source_files=source_records,catalogues=catalogs,firmware=[{k:v for k,v in x.items()if k!='sites'}for x in firmware])
(OUT/'census.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('catalogues', {k:len(v)for k,v in catalogs.items()})
for f in firmware:print(f['label'],'word/byte fields',len(f['fields']),'unique addresses',len(set(x['address']for x in f['fields'])),'DPTR sites',len(f['sites']))
