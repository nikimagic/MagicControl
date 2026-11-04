from pathlib import Path
import struct,json,hashlib
root=Path(r'C:\Users\admin\Documents\mechrevo\Research\BIOS_OTA_Full');exe=root/'binaries/BIOS_OTA.exe';data=exe.read_bytes();r=[]
for offset in (0x643994,0x927d0e):
 start=data.rfind(b'\x89PNG\r\n\x1a\n',max(0,offset-65536),offset)
 print(hex(offset),'PNG at',hex(start) if start>=0 else None,'before',data[offset-12:offset+4].hex(' '))
 if start>=0:
  print('IHDR',struct.unpack('>IIBBBBB',data[start+16:start+29]));width,height,depth,color,*_=struct.unpack('>IIBBBBB',data[start+16:start+29]);pos=start+8;chunks=[]
  while pos+12<=len(data):
   size=struct.unpack_from('>I',data,pos)[0];kind=data[pos+4:pos+8].decode('ascii',errors='replace');chunks.append({'offset':hex(pos),'kind':kind,'bytes':size});pos+=12+size
   if kind=='IEND':break
   if len(chunks)>200:break
  r.append({'zlib_offset':hex(offset),'png_offset':hex(start),'width':width,'height':height,'depth':depth,'color_type':color,'chunks':chunks,'exe_sha256':hashlib.sha256(data).hexdigest(),'png_sha256':hashlib.sha256(data[start:pos]).hexdigest(),'expanded_scanline_bytes':height*(1+width*4)})
Path('research/wujie-native-idle-20261021/local-assets-png.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
