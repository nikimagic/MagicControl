"""Resolve the six actual DynamicIL wrappers using their own decoded loader arguments.
All target operations are byte transforms/metadata reads. Never executes OEM code.
"""
from pathlib import Path
import json,struct,re,subprocess,hashlib
import argparse
_project=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file() and (p/'src').is_dir())
_parser=argparse.ArgumentParser(description=__doc__);_parser.add_argument('--directory',type=Path,default=_project/'build/research-hydroc16g2/exact-reproduction');_parser.add_argument('--decompiler',type=Path,default=_project/'build/research-hydroc16g2/decompiler/bin/Debug/net10.0/Decompile.exe');_args=_parser.parse_args()
r=_args.directory.resolve();tool=_args.decompiler.resolve();source=r/'GCUService.exact-jit-decrypted.analysis-only.dll';b=source.read_bytes();out=bytearray(b);layout=json.loads((r/'exact-jit-decryption.json').read_text())
assert hashlib.sha256(b).hexdigest()==layout['output_sha256']
tokens=[]
for _record in layout['entries']:
 _start=_record['offset']+_record['header_bytes'];_il=b[_start:_start+_record['il_bytes']]
 if b'\x28'+struct.pack('<I',0x0600199c) in _il:tokens.append(_record['token'])
assert len(tokens)==6
wrapper_dir=r/'inner-wrappers';wrapper_dir.mkdir(exist_ok=True)

method_name=r'\u000e\u0005\u0016\u000b\u0014\u0004';records=[]
k0=bytes([71,78,85,0,244,97,255,43,125,27,216,151,25,248,255,88,230,71,133,225,148,157,26,220,69,213,236,187,227,146,124,70]);master=bytes(((v^0x3c)-195)&255 for v in k0)
def rc4(key,data):
 s=list(range(256));j=0
 for i in range(256):j=(j+s[i]+key[i%len(key)])&255;s[i],s[j]=s[j],s[i]
 i=j=0;result=bytearray()
 for v in data:i=(i+1)&255;j=(j+s[i])&255;s[i],s[j]=s[j],s[i];result.append(v^s[(s[i]+s[j])&255])
 return bytes(result)
for token in tokens:
 text=subprocess.check_output([str(tool),str(source),'--method-token',format(token,'08X')],text=True,encoding='utf-8');(wrapper_dir/(format(token,'08X')+'.cs')).write_text(text,encoding='utf-8')
 pos=text.index(method_name+'(')+len(method_name)+1;end=text.index(');',pos);args=text[pos:end]
 values=[int(x) for x in re.findall(r'\b(\d+)u?\b',args)];assert len(values)==7,(hex(token),args,values)
 code,size,locals_rva,locals_size,sig_rva,key_rva,index=values;off=code-0x1e00;key=b[key_rva-0x1e00:key_rva-0x1e00+16];key=rc4(master,key);plain=rc4(key,b[off:off+size])
 if plain[0]&3==2:header=1;il_size=plain[0]>>2;local=0
 else:
  flags,stack,il_size,local=struct.unpack_from('<HHII',plain);assert flags&3==3;header=(flags>>12)*4;assert 12<=header<=60
 assert header+il_size<=len(plain),(hex(token),plain[:16].hex())
 out[off:off+size]=plain;struct.pack_into('<I',out,layout['method_table_offset']+((token&0xffffff)-1)*layout['method_row_size'],code)
 records.append({'token':token,'rva':code,'offset':off,'bytes':size,'il_bytes':il_size,'local_signature':local,'locals_rva':locals_rva,'locals_size':locals_size,'sig_rva':sig_rva,'key_rva':key_rva,'wrapper_index':index,'wrapper_arguments':args,'sha256':hashlib.sha256(plain).hexdigest()})
output=r/'GCUService.exact-all-layers.analysis-only.dll';output.write_bytes(out);(r/'exact-inner-decryption.json').write_text(json.dumps({'input_sha256':hashlib.sha256(b).hexdigest(),'output_sha256':hashlib.sha256(out).hexdigest(),'executed':False,'entries':records},indent=2));print('Exact inner DynamicIL bodies',len(records),[(hex(a['token']),a['il_bytes']) for a in records])
