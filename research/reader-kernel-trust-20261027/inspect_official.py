"""Inspect downloaded vendor archives/PEs as bytes, without execution."""
from pathlib import Path
import hashlib,io,json,struct,zipfile
OUT=Path(__file__).resolve().parent;ROOT=OUT.parents[1]
sha=lambda b:hashlib.sha256(b).hexdigest()
receipt=dict(vendor_executed=False,drivers_loaded=False,ami=[],nvidia_images=[])
with zipfile.ZipFile(OUT/'downloads/AMI-Aptio-V-current.zip') as outer:
    name=next(n for n in outer.namelist() if n.lower().endswith('afu/afuwin/64/afuwin64.zip'))
    with zipfile.ZipFile(io.BytesIO(outer.read(name))) as inner:
        for filename in ('AFUWINx64.exe','amigendrv64.sys'):
            member=next(n for n in inner.namelist() if Path(n).name.lower()==filename.lower())
            blob=inner.read(member);local=ROOT/'src/platform/windows/runtime/AmiAfu'/filename
            receipt['ami'].append(dict(member=member,bytes=len(blob),sha256=sha(blob),matches_bundled=blob==local.read_bytes()))
data=(OUT/'downloads/NVIDIA_UEFI_Firmware_Updater_2.0-x64.exe').read_bytes()
(OUT/'candidate-drivers').mkdir(exist_ok=True)
pos=0
while True:
    pos=data.find(b'MZ',pos)
    if pos<0:break
    try:
        pe=pos+struct.unpack_from('<I',data,pos+0x3c)[0]
        if pe<pos+0x40 or pe>pos+0x1000 or data[pe:pe+4]!=b'PE\0\0':raise ValueError()
        machine,sections,_,_,_,opt_size,flags=struct.unpack_from('<HHIIIHH',data,pe+4)
        if machine not in (0x8664,0x14c) or not 0<sections<96:raise ValueError()
        opt=pe+24;magic=struct.unpack_from('<H',data,opt)[0]
        if magic not in (0x20b,0x10b):raise ValueError()
        dd=opt+(112 if magic==0x20b else 96)
        cert_off,cert_size=struct.unpack_from('<II',data,dd+32)
        subsystem=struct.unpack_from('<H',data,opt+68)[0]
        rawend=0
        for i in range(sections):
            off=opt+opt_size+40*i
            rawsize,rawptr=struct.unpack_from('<II',data,off+16)
            rawend=max(rawend,rawptr+rawsize)
        length=max(rawend,cert_off+cert_size)
        if pos+length>len(data) or length<1024:raise ValueError()
        blob=data[pos:pos+length]
        row=dict(offset=pos,bytes=length,subsystem=subsystem,sha256=sha(blob),signed_certificate_bytes=cert_size)
        if subsystem==1:
            name=f'nvidia-driver-{pos:08x}.sys';(OUT/'candidate-drivers'/name).write_bytes(blob);row['file']=name
        receipt['nvidia_images'].append(row)
    except (ValueError,struct.error):pass
    pos+=2
(OUT/'OFFICIAL.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
print(json.dumps(receipt,indent=2))
