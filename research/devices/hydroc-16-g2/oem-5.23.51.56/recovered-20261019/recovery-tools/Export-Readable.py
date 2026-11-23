"""Display-only pass: pure literal decoder inlining and exact proxy annotations.
Raw decompiler output is preserved separately; no resulting source is executed.
"""
from pathlib import Path
import re,json,collections,hashlib
import argparse,subprocess,struct
root=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file() and (p/'src').is_dir())
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--directory',type=Path,default=root/'out/research-hydroc16g2/exact-reproduction')
parser.add_argument('--destination',type=Path,default=root/'out/research-hydroc16g2/recovered-20261019')
parser.add_argument('--input',type=Path,default=root/'research/research-hydroc16g2/installer/app/AiStoneService/MyControlCenter/GCUService.exe')
parser.add_argument('--decompiler',type=Path,default=root/'research/research-hydroc16g2/decompiler/bin/Debug/net10.0/Decompile.exe')
parser.add_argument('--reuse-raw',action='store_true',help='Reuse this pinned sample raw C# export')
args=parser.parse_args();r=args.directory.resolve();r.mkdir(parents=True,exist_ok=True);dest=args.destination.resolve();tool=args.decompiler.resolve()
historical=root/'research/research-hydroc16g2/exact-reproduction'
use_historical=r==(root/'out/research-hydroc16g2/exact-reproduction').resolve()
input_path=lambda name:(r/name) if (r/name).is_file() or not use_historical else (historical/name)
source=input_path('GCUService.exact-all-layers.analysis-only.dll')
assert hashlib.sha256(source.read_bytes()).hexdigest()==json.loads(input_path('exact-inner-decryption.json').read_text())['output_sha256']
if not args.reuse_raw:
 subprocess.run([str(tool),str(source),'--tree-token',str(r/'final-raw')],check=True)
 for token in ['06000001','06000002']:
  text=subprocess.check_output([str(tool),str(source),'--method-token',token],text=True,encoding='utf-8');(r/('Module-'+token+'.cs')).write_text(text,encoding='utf-8')
subprocess.run([str(tool),str(source),'--names',str(r/'metadata-names.exact.json')],check=True)
names={int(k):v for k,v in json.loads((r/'metadata-names.exact.json').read_text()).items()}
original=args.input.read_bytes();assert hashlib.sha256(original).hexdigest()=='e5d388b45515d06567c2e71e4c2b882a1fa9313704d70785f19aec960e361003'
p=0x85f110-0x1e00;n=struct.unpack_from('<I',original,p)[0];assert n==724;entries=[]
for index in range(n):
 target,invoke,kind,reserved=struct.unpack_from('<IIII',original,p+8+16*index)
 assert target in names and invoke in names and kind in [0,1,2,4,8,16]
 entries.append({'index':index,'target_token':target,'target':names[target],'invoke_token':invoke,'invoke':names[invoke],'kind':kind})
(r/'import-proxies.exact.json').write_text(json.dumps({'source_rva':0x85f110,'count':n,'entries':entries},indent=2,ensure_ascii=True))
raw=dest/'raw-csharp';readable=dest/'readable-csharp';raw.mkdir(parents=True,exist_ok=True);readable.mkdir(exist_ok=True)

def escaped(chars):return ''.join(r'\u%04x'%c for c in chars)
dec_type=escaped([11,2,23,6,13,18]);dec_name=escaped([7,10,7,10,5,4,11,9]);ident=r'(?:\\u[0-9a-fA-F]{4})+'
cs_literal=r'@"(?:[^"]|"")*"|"(?:\\.|[^"\\])*"'
decoder=re.compile(r'(?:global::)?(?:'+ident+r'\.)*'+re.escape(dec_type)+r'\.'+re.escape(dec_name)+r'\(\s*('+cs_literal+r')\s*\)')
proxy_type=escaped([4,5,7,13,7,8,3,6]);proxy_field=escaped([7,4,9,5,2,8,5,9,3,2]);proxy_pattern=re.compile(r'(?:global::)?(?:'+ident+r'\.)*'+re.escape(proxy_type)+r'\.'+re.escape(proxy_field)+r'\[(\d+)\]')
proxies={a['index']:a for a in json.loads((r/'import-proxies.exact.json').read_text())['entries']}
def parse_literal(s):
 if s.startswith('@"'):return s[2:-1].replace('""','"')
 s=s[1:-1];out=[];i=0;escapes={'0':'\0','a':'\a','b':'\b','f':'\f','n':'\n','r':'\r','t':'\t','v':'\v','\\':'\\','"':'"',"'":"'"}
 while i<len(s):
  if s[i]!='\\':out.append(s[i]);i+=1;continue
  i+=1;c=s[i];i+=1
  if c in escapes:out.append(escapes[c]);continue
  if c in ['u','U']:
   n=4 if c=='u' else 8;out.append(chr(int(s[i:i+n],16)));i+=n;continue
  if c=='x':
   m=re.match(r'[0-9a-fA-F]{1,4}',s[i:]);assert m;out.append(chr(int(m[0],16)));i+=len(m[0]);continue
  raise ValueError('Unsupported C# escape '+c)
 return ''.join(out)
def decode_literal(s):
 units=parse_literal(s).encode('utf-16le','surrogatepass');n=len(units)//2;key=(n^0x58)&255;out=bytearray()
 for i in range(n):v=int.from_bytes(units[2*i:2*i+2],'little');v=((v+10+i)^key)&65535;out.extend(v.to_bytes(2,'little'))
 text=out.decode('utf-16le','surrogatepass');quoted=json.dumps(text,ensure_ascii=False)
 quoted=''.join(r'\u%04x'%ord(c) if 0xd800<=ord(c)<=0xdfff or ord(c) in [0x2028,0x2029] else c for c in quoted)
 return quoted
counts=collections.Counter();manifest=[]
saved_raw=root/'research/devices/hydroc-16-g2/oem-5.23.51.56/recovered-20261019/raw-csharp'
if args.reuse_raw and not (r/'final-raw').is_dir():
 source_files=sorted(saved_raw.glob('*.cs'))
 if not source_files:raise FileNotFoundError('Saved raw C# export is unavailable')
else:
 source_files=list((r/'final-raw').glob('*.cs'))+[r/'Module-06000001.cs',r/'Module-06000002.cs']
for source in source_files:
 text=source.read_text(encoding='utf-8-sig');(raw/source.name).write_text(text,encoding='utf-8')
 def inline(m):counts['decoded_literal_calls']+=1;return decode_literal(m[1])
 text=decoder.sub(inline,text)
 def annotate(m):
  index=int(m[1]);entry=proxies.get(index)
  if entry is None:counts['unknown_proxy_indices']+=1;return m[0]
  counts['proxy_annotations']+=1;name=entry['target'].replace('*/','* /');name=''.join(r'\u%04x'%ord(c) if ord(c)<32 else c for c in name)
  return '/* import '+str(index)+': '+name+' */ '+m[0]
 text=proxy_pattern.sub(annotate,text)
 text,removed=re.subn(r'\s*if\s*\((?:1 == 0|false)\)\s*\{\s*_ = -?\d+;\s*\}','',text);counts['constant_false_noops_removed']+=removed
 header='// Static OEM decompilation; target code was NOT executed.\n// Display pass only: literal decoding / exact import-table annotations.\n// Raw source and extraction proofs are in sibling folders; see README.txt.\n'
 (readable/source.name).write_text(header+text,encoding='utf-8');manifest.append({'file':source.name,'raw_sha256':hashlib.sha256((raw/source.name).read_bytes()).hexdigest(),'readable_sha256':hashlib.sha256((readable/source.name).read_bytes()).hexdigest()})
counts['type_files']=len(source_files)-2;counts['module_method_files']=2;counts['raw_decompiler_il_errors']=sum(p.read_text(encoding='utf-8').count('Error near IL_') for p in raw.glob('*.cs'));counts['raw_runtime_exception_throws']=sum(p.read_text(encoding='utf-8').count('throw new Exception("Runtime exception")') for p in raw.glob('*.cs'))
(dest/'source-manifest.json').write_text(json.dumps({'counts':counts,'files':manifest},indent=2));print('Persistent raw/readable tree:',dest);print(dict(counts))
