from pathlib import Path
import json,struct,zlib,hashlib
out=Path('research/wujie-native-idle-20261021');base=Path(r'C:\Users\admin\Documents\mechrevo\Research\BIOS_OTA_Full');data=(base/'binaries/BIOS_OTA.exe').read_bytes();r=json.loads((out/'local-assets-png.json').read_text(encoding='utf-8-sig'))
for item in r:
 start=int(item['png_offset'],16);pos=start+8;payload=b'';checks=[]
 while True:
  n=struct.unpack_from('>I',data,pos)[0];kind=data[pos+4:pos+8];b=data[pos+8:pos+8+n];crc=struct.unpack_from('>I',data,pos+8+n)[0];checks.append((zlib.crc32(kind+b)&0xffffffff)==crc)
  if kind==b'IDAT':payload+=b
  pos+=12+n
  if kind==b'IEND':break
 raw=zlib.decompress(payload);file=base/'BIOS_OTA/assets'/(f"{int(item['zlib_offset'],16):08x}.bin")
 item.update(all_chunk_crcs_valid=all(checks),idat_expansion_matches_asset=raw==file.read_bytes(),expanded_sha256=hashlib.sha256(raw).hexdigest(),classification='PNG image data, not EC code')
(out/'local-assets-png.json').write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
print([(x['png_offset'],x['all_chunk_crcs_valid'],x['idat_expansion_matches_asset'])for x in r])
a=json.loads((out/'sources/official-catalog-first.body').read_text(encoding='utf-8-sig'))['result'];b=json.loads(Path('research/analysis-20261020/wujie-ec-firmware/sources/driver-pages-500/page-1.json').read_text(encoding='utf-8-sig'))['result'];(out/'catalog-comparison.json').write_text(json.dumps({'current_total':a['total'],'previous_total':b['total'],'retrieved_current_records':len(a['objects']),'page_1_objects_equal':{x['id']:x for x in a['objects']}=={x['id']:x for x in b['objects']},'scope':'Fresh first500 records match saved prior scan. This is not a new complete retrieval of all3673 records.'},indent=2)+'\n',encoding='utf-8')

