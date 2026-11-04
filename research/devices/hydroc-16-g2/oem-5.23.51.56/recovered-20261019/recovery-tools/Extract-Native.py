from pathlib import Path
import struct,json,hashlib
import argparse
_project=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file() and (p/'src').is_dir())
_parser=argparse.ArgumentParser(description=__doc__)
_parser.add_argument('--input',type=Path,default=_project/'build/research-hydroc16g2/installer/app/AiStoneService/MyControlCenter/GCUService.exe')
_parser.add_argument('--output',type=Path,default=_project/'build/research-hydroc16g2/exact-reproduction')
_args=_parser.parse_args();r=_args.output.resolve();r.mkdir(parents=True,exist_ok=True);src=_args.input.resolve()

def rc4(key,data):
 s=list(range(256));j=0
 for i in range(256):j=(j+s[i]+key[i%len(key)])&255;s[i],s[j]=s[j],s[i]
 i=j=0;out=bytearray()
 for v in data:i=(i+1)&255;j=(j+s[i])&255;s[i],s[j]=s[j],s[i];out.append(v^s[(s[i]+s[j])&255])
 return bytes(out)
original=src.read_bytes()
assert hashlib.sha256(original).hexdigest()=='e5d388b45515d06567c2e71e4c2b882a1fa9313704d70785f19aec960e361003'
_payload=0x7de6d8-0x1e00;_size=struct.unpack_from('<I',original,_payload)[0]
assert 0<_size<0x1000000 and _payload+24+_size<=len(original)
data=rc4(original[_payload+8:_payload+24],original[_payload+24:_payload+24+_size])
(r/'native-x64-decoded.bin').write_bytes(data)
u16=lambda o:struct.unpack_from('<H',data,o)[0];u32=lambda o:struct.unpack_from('<I',data,o)[0];u64=lambda o:struct.unpack_from('<Q',data,o)[0]
size=u32(34);image=bytearray(size);nsec=u16(6);sections=[]
for i in range(nsec):
 p=94+36*i;va,vs,n,off,flags=struct.unpack_from('<IIIII',data,p);key=data[p+20:p+36]
 assert va+n<=size and off+n<=len(data)
 plain=rc4(key,data[off:off+n]) if n else b'';image[va:va+n]=plain
 sections.append({'rva':va,'virtual_size':vs,'raw_size':n,'source_offset':off,'flags':flags})
# Reconstruct a conventional PE for STATIC analysis. No loading, imports or entrypoint calls.
image[:2]=b'MZ';struct.pack_into('<I',image,60,0x80);image[0x80:0x84]=b'PE\0\0'
struct.pack_into('<HHIIIHH',image,0x84,u16(4),nsec,0,0,0,0xf0,u16(10));opt=0x98
struct.pack_into('<H',image,opt,0x20b);struct.pack_into('<I',image,opt+16,u32(14));struct.pack_into('<I',image,opt+20,min(s['rva'] for s in sections if s['flags']&0x20));struct.pack_into('<Q',image,opt+24,u64(18));struct.pack_into('<II',image,opt+32,0x1000,0x200);struct.pack_into('<HH',image,opt+40,6,0);struct.pack_into('<HH',image,opt+48,6,0);struct.pack_into('<II',image,opt+56,size,0x400);struct.pack_into('<HH',image,opt+68,u16(42),u16(44));struct.pack_into('<QQQQ',image,opt+72,0x100000,0x1000,0x100000,0x1000);struct.pack_into('<I',image,opt+108,16)
for i in range(6):struct.pack_into('<II',image,opt+112+8*i,u32(46+8*i),u32(50+8*i))
for i,s in enumerate(sections):
 p=opt+0xf0+40*i;name=('.s%d'%i).encode();image[p:p+8]=name.ljust(8,b'\0');struct.pack_into('<IIIIIIHHI',image,p+8,s['virtual_size'],s['rva'],s['raw_size'],s['rva'],0,0,0,0,s['flags'])
out=r/'native-x64.static-analysis-only.dll';out.write_bytes(image)
# Exports are read from the reconstructed image, never resolved by LoadLibrary.
exp=u32(46);nr=struct.unpack_from('<I',image,exp+24)[0];fn,ns,ords=struct.unpack_from('<III',image,exp+28);exports={}
for i in range(nr):
 p=struct.unpack_from('<I',image,ns+4*i)[0];name=bytes(image[p:image.index(0,p)]).decode('ascii');ordinal=struct.unpack_from('<H',image,ords+2*i)[0];exports[name]=struct.unpack_from('<I',image,fn+4*ordinal)[0]
info={'input_sha256':hashlib.sha256(data).hexdigest(),'output_sha256':hashlib.sha256(image).hexdigest(),'image_base':u64(18),'entrypoint':u32(14),'sections':sections,'exports':exports,'executed':False}
(r/'native-x64-extraction.json').write_text(json.dumps(info,indent=2));print('Native image statically reconstructed:',hex(size),'sections',nsec,'exports',{k:hex(v) for k,v in exports.items()})
