"""Extract AMI UCP @ROM through a synthetic UEFI compression wrapper.
No updater/firmware/driver is executed. Only the trusted UEFIExtract file parser.
Format reference: https://github.com/platomav/BIOSUtilities/blob/main/biosutilities/ami_ucp_extract.py
"""
from pathlib import Path
import hashlib,json,struct,uuid,subprocess
ROOT=Path(__file__).resolve().parents[5]
BASE=Path(__file__).resolve().parent.parent
SOURCE=ROOT/'research/gm6-battery-firmware-20261021/GMxIR7xN105MRO09_CAP.exe'
WORK=ROOT/'out/gm6-battery-firmware-20261021'/('reproduce-ucp-'+uuid.uuid4().hex);WORK.mkdir(parents=True,exist_ok=False)
data=SOURCE.read_bytes();sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(data)=='927efdc56ed3b1b434be86ef3d8d6c40318695817b89ca455a0230a7e264dc1a'
pe=struct.unpack_from('<I',data,0x3c)[0];count=struct.unpack_from('<H',data,pe+6)[0];size=struct.unpack_from('<H',data,pe+20)[0];opt=pe+24
end=max(struct.unpack_from('<I',data,opt+size+i*40+20)[0]+struct.unpack_from('<I',data,opt+size+i*40+16)[0]for i in range(count))
root=data.find(b'@UAF',end);assert root==end+16
length=struct.unpack_from('<I',data,root+4)[0];container=data[root:root+length]
assert len(container)==length and length%2==0 and sum(struct.unpack('<'+str(length//2)+'H',container))&65535==0
at=16;modules=[];rom=None
while at+24<=len(container):
 tag=container[at:at+4].decode('ascii');n=struct.unpack_from('<I',container,at+4)[0]
 assert tag.startswith('@') and 24<=n<=len(container)-at
 module=container[at:at+n];assert n%2==0 and sum(struct.unpack('<'+str(n//2)+'H',module))&65535==0
 comp,orig=struct.unpack_from('<II',module,16);raw=module[24:]
 assert comp<=16*1024*1024 and orig<=64*1024*1024 and max(0,comp-len(raw))<=16
 payload=module[16:24]+raw+b'\0'*max(0,comp-len(raw))
 modules.append({'tag':tag,'offset':root+at,'module_size':n,'compressed':comp,'original':orig,'payload_sha256':sha(payload)})
 if tag=='@ROM':rom=payload
 at+=n
assert at==len(container)and rom
orig=struct.unpack_from('<I',rom,4)[0]
sec=struct.pack('<I',9+len(rom))[:3]+b'\x01'+struct.pack('<I',orig)+b'\x01'+rom
file=bytearray(uuid.UUID('ffffffff-1111-2222-3333-444444444444').bytes_le+b'\0\0'+b'\x02\0'+struct.pack('<I',24+len(sec))[:3]+b'\0')
file[16]=(-sum(file))&255;file[17]=0xaa;file[23]=0xf8
n=((72+24+len(sec)+4095)//4096)*4096
header=bytearray(b'\0'*16+uuid.UUID('8c8ce578-8a3d-4f1c-9935-896185c32dd3').bytes_le+struct.pack('<Q',n)+b'_FVH'+struct.pack('<I',0x0004feff)+struct.pack('<HHHBB',72,0,0,0,2)+struct.pack('<IIII',n//4096,4096,0,0))
struct.pack_into('<H',header,50,(-sum(struct.unpack('<36H',header)))&65535)
wrapper=WORK/'synthetic-wrapper.fv';wrapper.write_bytes(header+file+sec+b'\xff'*(n-72-24-len(sec)))
parser=ROOT/'../Decompile/HONOR_HLYL-WXX9/honor-analysis/uefiextract/UEFIExtract.exe'
result=subprocess.run([str(parser),str(wrapper),'all'],capture_output=True,text=True,errors='replace')
(WORK/'parser.log').write_text(result.stdout+result.stderr,encoding='utf-8');assert result.returncode==0
hits=[p for p in Path(str(wrapper)+'.dump').rglob('body.bin')if p.stat().st_size==orig]
assert len(hits)==1
bios=hits[0].read_bytes();assert sha(bios)=='e6d9205000bfa888e48f4499f620f7d9725334827dbbb2e750388bb4b51ff688'
(WORK/'BIOS-MRO09.bin').write_bytes(bios)
(WORK/'manifest.json').write_text(json.dumps({'source_sha256':sha(data),'modules':modules,'BIOS_bytes':len(bios),'BIOS_sha256':sha(bios),'UEFIExtract_sha256':sha(parser.read_bytes()),'warning':'Synthetic wrapper exists only to decode compression, never flash it.'},indent=2)+'\n',encoding='utf-8')
print('Extracted BIOS bytes',len(bios),'SHA256',sha(bios))
