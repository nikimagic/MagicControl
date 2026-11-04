"""Verify downloaded vendor archives as data. Never execute EXE/BAT/EFI/SYS."""
from pathlib import Path
import hashlib,io,json,re,zipfile
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
sha=lambda data:hashlib.sha256(data).hexdigest()
local=ROOT.parent/'BIOS Unlock/FPTW64.exe';original=local.read_bytes()
assert sha(original)=='15d329ce4e5cbce10ad690e0727c54ffbbbb2a0489f80919c1b3b37858564aaa'
def versions(data):
    return sorted(set(s.group().decode('utf-16-le') for s in re.finditer(rb'(?:[\x20-\x7e]\x00){5,}',data)
                      if re.fullmatch(r'16\.\d+\.\d+\.\d+',s.group().decode('utf-16-le'))))
archive=OUT/'GIGABYTE_BIOS_NHxxPNP_RCxxKE_FB05.zip'
with zipfile.ZipFile(archive) as package:
    member='BIOS_NHxxPNP(RCxxKE)FB05/ALL/FPTW64.exe'
    official=package.read(member)
assert official==original
zone=Path(str(local)+':Zone.Identifier').read_text(encoding='utf-8-sig')
original_archive=ROOT.parent/'BIOS Unlock.rar'
result={'analysis_date_moscow':'2026-10-25','static_only':True,'hardware_executed':False,
        'local':{'path':str(local),'bytes':len(original),'sha256':sha(original),'versions':versions(original),'zone_identifier':zone,
                 'source_archive':str(original_archive),'source_archive_sha256':sha(original_archive.read_bytes()),
                 'original_internet_source_recorded':False},
        'official':{'publisher':'GIGABYTE','page':'https://www.gigabyte.com/Laptop/G5--Intel-12th-Gen/support',
                    'page_title':'G5 (Intel 12th Gen) Laptop Support - GIGABYTE Global','selected_model':'G5 KE',
                    'observed_bios_row':'BIOS FB05 | FB05 | 22.55 MB | Mar 7, 2023',
                    'page_link':'https://download.gigabyte.com/FileList/BIOS/BIOS_NHxxPNP_RCxxKE_FB05.zip?v=e1bbe4f77e1fe23e8ac0a96e98608750',
                    'requested_url':'https://download.gigabyte.com/FileList/BIOS/BIOS_NHxxPNP_RCxxKE_FB05.zip',
                    'archive_path':str(archive),'archive_bytes':archive.stat().st_size,'archive_sha256':sha(archive.read_bytes()),
                    'member':member,'fpt_bytes':len(official),'fpt_sha256':sha(official),'versions':versions(official),
                    'byte_equal_to_local':official==original},
        'qualification':'Exact OEM distribution match; it does not reconstruct the path by which a third party assembled BIOS Unlock.rar.'}

other=OUT/'gigabyte-g5g7-fb10.zip'
with zipfile.ZipFile(other) as package:
    data=package.read('FPTW64.exe')
    result['different_official_fpt']={'url':'https://download.gigabyte.com/FileList/BIOS/nb-bios-g5_g7-vf-win11-64bit-ddr4-bios-fb10.zip',
                                    'archive_sha256':sha(other.read_bytes()),'bytes':len(data),'sha256':sha(data),'versions':versions(data),'same_as_local':data==original}

ami=OUT/'ami-current-aptio-v.zip'
with zipfile.ZipFile(ami) as package:
    inner=next(n for n in package.namelist() if n.lower().endswith('afuwin/64/afuwin64.zip'))
    with zipfile.ZipFile(io.BytesIO(package.read(inner))) as win:
        components=[]
        for name in ['AFUWINx64.exe','amigendrv64.sys','readme.txt']:
            entry=next(n for n in win.namelist() if n.endswith('/'+name))
            data=win.read(entry);ours=ROOT/'src/platform/windows/runtime/AmiAfu'/name
            components.append({'member':entry,'bytes':len(data),'sha256':sha(data),'byte_equal_to_embedded_resource':data==ours.read_bytes()})
        assert all(c['byte_equal_to_embedded_resource'] for c in components)
result['ami_recheck']={'url':'https://f.hubspotusercontent10.net/hubfs/9443417/Support/BIOS_Firmware_Update/Aptio_V_AMI_Firmware_Update_Utility.zip',
                       'archive_bytes':ami.stat().st_size,'archive_sha256':sha(ami.read_bytes()),'version':'5.17.00.0153','components':components,
                       'newer_build_found_at_this_url':False,'all_possible_oem_afu_versions_checked':False}

baseline=json.loads((ROOT/'research/bios-unlock-package-20261024/EVIDENCE.json').read_text(encoding='utf-8'))
assert all(sha((ROOT.parent/'BIOS Unlock'/f['name']).read_bytes())==f['sha256'] for f in baseline['files'])
result['original_package_files_unchanged']=len(baseline['files'])
(OUT/'EVIDENCE.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'exact_official_fpt_match':result['official'],'ami_recheck':result['ami_recheck']},ensure_ascii=False,indent=2))
