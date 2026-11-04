"""Compare saved AMD/Intel captures and reference images; no hardware or subprocess."""
from pathlib import Path
import hashlib,json,re,struct,uuid,zipfile
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
z=zipfile.ZipFile(ROOT/'Users_logs/logs_GM6IR0C (15).zip')
prefix='logs_GM6IR0C/bios-dumps/bios-20261024-203144-0/'
intel=z.read(prefix+'pass1.partial.bin')
events=[json.loads(x) for x in z.read(prefix+'events.jsonl').splitlines()]
def volumes(b):
    result=[];at=0
    while True:
        i=b.find(b'_FVH',at)
        if i<0:break
        at=i+4;start=i-40
        if start<0 or start+56>len(b):continue
        length=struct.unpack_from('<Q',b,start+32)[0];header=struct.unpack_from('<H',b,start+48)[0]
        if not(56<=header<=4096 and header%2==0 and header<=length and start+length<=len(b)):continue
        valid=sum(struct.unpack_from('<'+'H'*(header//2),b,start))&0xffff==0
        result.append({'offset':hex(start),'bytes':length,'header_bytes':header,'checksum_valid':valid,'filesystem_guid':str(uuid.UUID(bytes_le=b[start+16:start+32]))})
    return result
report={'intel':{'archive_member':prefix+'pass1.partial.bin','bytes':len(intel),'sha256':sha(intel),'precheck':json.loads(z.read(prefix+'precheck.json')),'manifest':json.loads(z.read(prefix+'manifest.json')),'events':events,'stderr_bytes':len(z.read(prefix+'pass1.stderr.log')),'flash_descriptor_at_0x10':intel[16:20].hex(),'uefi_volumes':volumes(intel),'references':[]}}
units=json.loads((ROOT/'research/ec-platform-catalogue-20261023/source-units.json').read_text(encoding='utf-8'))
for u in units:
    if u.get('sha256') not in ['2039277e2adfc54ec11983fd5822dcc070648b41217fe86b57a3f5779f2f7722','ac379a0bfbf9aa079e92d4e386b4f80eafbbe72f3f23b66a8d1f8c0ce53a5e12']:continue
    raw=Path(u['path']).read_bytes()
    if sha(raw)!=u['sha256']:raise ValueError('Reference bytes changed')
    frba=((struct.unpack_from('<I',raw,20)[0]>>16)&255)*16;regions=[]
    for i,name in enumerate(['Descriptor','BIOS','ME','GbE','PlatformData']):
        v=struct.unpack_from('<I',raw,frba+i*4)[0];base=(v&32767)*4096;limit=((v>>16)&32767)*4096+4095;enabled=limit>=base
        regions.append({'region':name,'base':hex(base),'limit':hex(limit),'enabled':enabled,'bytes':limit-base+1 if enabled else 0})
    bios_region=regions[1];base=int(bios_region['base'],16);bios=raw[base:int(bios_region['limit'],16)+1]
    bad=[]
    for i in range(0,len(intel),4096):
        if intel[i:i+4096]!=bios[i:i+4096]:
            if bad and bad[-1][1]+4096==i:bad[-1][1]=i
            else:bad.append([i,i])
    same=sum(x==y for x,y in zip(intel,bios))
    report['intel']['references'].append({'source':u['path'],'sha256':sha(raw),'bytes':len(raw),'descriptor_regions':regions,'bios_region_sha256':sha(bios),'equal_bytes':same,'equal_fraction':same/len(intel),'identical_4k_blocks':sum(intel[i:i+4096]==bios[i:i+4096] for i in range(0,len(intel),4096)),'total_4k_blocks':len(intel)//4096,'differing_4k_spans':[[hex(x),hex(y+4095)] for x,y in bad],'reset_vector_16_bytes_identical':intel[-16:]==bios[-16:]})
ec=next(u for u in units if u.get('id')=='EC:5059e6cb58ad')
ecbytes=Path(ec['path']).read_bytes();found=intel.find(ecbytes)
report['intel']['embedded_ec_update_image']={'reference_sha256':sha(ecbytes),'bytes':len(ecbytes),'found_at_bios_offset':hex(found) if found>=0 else None,'live_ec_flash_dump':False}
amd=ROOT/'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/bios-dumps/bios-20261024-193321-0'
a=(amd/'bios.bin').read_bytes();v=(amd/'bios-verify.bin').read_bytes()
report['amd']={'directory':str(amd),'precheck':json.loads((amd/'precheck.json').read_text(encoding='utf-8-sig')),'manifest':json.loads((amd/'manifest.json').read_text(encoding='utf-8-sig')),'pass1_sha256':sha(a),'pass2_sha256':sha(v),'bytes':len(a),'passes_equal':a==v,'stderr_bytes':[(amd/f'pass{i}.stderr.log').stat().st_size for i in [1,2]],'save_results':[json.loads(x)['details'] for x in (amd/'events.jsonl').read_text(encoding='utf-8-sig').splitlines() if json.loads(x)['event']=='save_returned']}
stdout=z.read(prefix+'pass1.stdout.log').decode('utf-8',errors='replace')
lines=[x.strip() for x in re.sub(r'[\x00-\x08\x0b-\x1f]','\n',stdout).splitlines() if x.strip()]
report['intel']['stdout_end']=lines[-5:]
(OUT/'bios-comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'intel_bytes':len(intel),'intel_valid_fv_headers':sum(x['checksum_valid'] for x in report['intel']['uefi_volumes']),'intel_invalid_fv_headers':sum(not x['checksum_valid'] for x in report['intel']['uefi_volumes']),'embedded_ec':report['intel']['embedded_ec_update_image'],'differences_from_patched':report['intel']['references'][1]['differing_4k_spans'],'amd_bytes':len(a),'amd_equal':a==v},ensure_ascii=False,indent=2))
