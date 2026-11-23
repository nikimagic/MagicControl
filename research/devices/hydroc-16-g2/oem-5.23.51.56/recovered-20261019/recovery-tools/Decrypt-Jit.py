"""Static-only Virbox JIT-body extraction for the pinned Eluktronics OEM sample.
No Assembly.Load, target execution, imports or driver calls. Key recovered from
native FUN_180001b50 (RC4 KSA/PRGA calls), then verified by EACH VBPD footer.
Earlier homologous/ordered recovery candidates are NOT inputs to this script.
"""
from pathlib import Path
import struct,json,hashlib,re
import argparse
_project=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file() and (p/'src').is_dir())
_parser=argparse.ArgumentParser(description=__doc__)
_parser.add_argument('--input',type=Path,default=_project/'research/research-hydroc16g2/installer/app/AiStoneService/MyControlCenter/GCUService.exe')
_parser.add_argument('--output',type=Path,default=_project/'out/research-hydroc16g2/exact-reproduction')
_args=_parser.parse_args();r=_args.output.resolve();r.mkdir(parents=True,exist_ok=True);src=_args.input.resolve()
b=src.read_bytes()
expected='e5d388b45515d06567c2e71e4c2b882a1fa9313704d70785f19aec960e361003'
assert hashlib.sha256(b).hexdigest()==expected,'Unexpected OEM sample'
pe=struct.unpack_from('<I',b,60)[0];opt=pe+24;magic=struct.unpack_from('<H',b,opt)[0];cli=struct.unpack_from('<I',b,opt+(96 if magic==0x10b else 112)+14*8)[0];nsec=struct.unpack_from('<H',b,pe+6)[0];section_start=opt+struct.unpack_from('<H',b,pe+20)[0];sections=[]
for i in range(nsec):
 p=section_start+40*i;vs,va,rs,off=struct.unpack_from('<IIII',b,p+8);sections.append((va,max(vs,rs),off))
def offset(va):
 for base,size,off in sections:
  if base<=va<base+size:return off+va-base
 raise ValueError('Unmapped RVA '+hex(va))
root=offset(struct.unpack_from('<I',b,offset(cli)+8)[0]);p=root+16+((struct.unpack_from('<I',b,root+12)[0]+3)&~3);count=struct.unpack_from('<H',b,p+2)[0];p+=4;streams={}
for _ in range(count):
 off,size=struct.unpack_from('<II',b,p);end=b.index(0,p+8);name=b[p+8:end].decode();p=(end+4)&~3;streams[name]=(root+off,size)
tables=streams['#~'][0];heap_flags=b[tables+6];valid=struct.unpack_from('<Q',b,tables+8)[0];rows={};p=tables+24
for i in range(64):
 if valid>>i&1:rows[i]=struct.unpack_from('<I',b,p)[0];p+=4
index=lambda table:4 if rows.get(table,0)>=65536 else 2
coded=lambda bits,ts:4 if max(rows.get(t,0) for t in ts)>=(1<<(16-bits)) else 2
st=4 if heap_flags&1 else 2;guid=4 if heap_flags&2 else 2;blob=4 if heap_flags&4 else 2
sizes={0:2+st+3*guid,1:coded(2,[0,26,35,1])+2*st,2:4+2*st+coded(2,[2,1,27])+index(4)+index(6),3:index(4),4:2+st+blob,5:index(6),6:8+st+blob+index(8)}
method_table=p+sum(rows.get(i,0)*sizes[i] for i in range(6));method_row=sizes[6]
key=struct.pack('<IIII',0x1ab1215f,0x2317dbea,0x1c9f87b5,0x9bfc772d)
def rc4(data):
 s=list(range(256));j=0
 for i in range(256):j=(j+s[i]+key[i%16])&255;s[i],s[j]=s[j],s[i]
 i=j=0;out=bytearray()
 for v in data:i=(i+1)&255;j=(j+s[i])&255;s[i],s[j]=s[j],s[i];out.append(v^s[(s[i]+s[j])&255])
 return bytes(out)
heads=[]
for hit in re.finditer(b'FWSN',b):
 start=hit.start();n=struct.unpack_from('<I',b,start+4)[0]
 if 0<n<=rows[6] and start+8+24*n<=len(b):heads.append((start,n))
assert len(heads)==1,'Ambiguous JIT descriptor table'
start,n=heads[0];out=bytearray(b);entries=[];seen=set();stub_rva=0x866f98
before=sum(struct.unpack_from('<I',b,method_table+i*method_row)[0]==stub_rva for i in range(rows[6]))
for i in range(n):
 rec=start+8+24*i;token,rel,rva,size,local_rel,other=struct.unpack_from('<IiIIiI',b,rec);rid=token&0xffffff
 assert token>>24==6 and 0<rid<=rows[6] and token not in seen;seen.add(token)
 pos=rec+8+rel;assert pos==offset(rva) and 4<=size<=0x1000000 and 0<=pos<=len(b)-size
 plain=rc4(b[pos:pos+size]);assert plain[-4:]==b'VBPD',hex(token)+' footer mismatch'
 if plain[0]&3==2:header=1;code_size=plain[0]>>2;local=0
 else:
  flags,stack,code_size,local=struct.unpack_from('<HHII',plain,0);assert flags&3==3;header=(flags>>12)*4;assert 12<=header<=60
  assert local==0 or local>>24==17 and (local&0xffffff)<=rows.get(17,0)
 assert header+code_size<=len(plain)-4
 previous=struct.unpack_from('<I',b,method_table+(rid-1)*method_row)[0];assert previous==stub_rva
 out[pos:pos+size]=plain;struct.pack_into('<I',out,method_table+(rid-1)*method_row,rva)
 entries.append({'token':token,'rva':rva,'offset':pos,'bytes':size,'header_bytes':header,'il_bytes':code_size,'local_signature':local,'sha256':hashlib.sha256(plain).hexdigest(),'footer':'VBPD'})
after=sum(struct.unpack_from('<I',out,method_table+i*method_row)[0]==stub_rva for i in range(rows[6]));assert before-after==n
# A checksum/signature is not regenerated: this image is ONLY a decompiler input.
output=r/'GCUService.exact-jit-decrypted.analysis-only.dll';output.write_bytes(out)
result={'input_sha256':expected,'output_sha256':hashlib.sha256(out).hexdigest(),'executed':False,'method_table_offset':method_table,'method_row_size':method_row,'descriptor_offset':start,'descriptor_count':n,'original_common_stub_count':before,'remaining_common_stub_count':after,'key_source':'native-x64 static FUN_180001b50','entries':entries}
(r/'exact-jit-decryption.json').write_text(json.dumps(result,indent=2));print('EXACT JIT extraction:',n,'VBPD-verified bodies;',before,'->',after,'common stubs; no heuristic inputs')
