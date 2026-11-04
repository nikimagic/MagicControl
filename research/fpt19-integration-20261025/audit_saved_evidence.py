"""File-only comparison with0.1.190 and saved firmware evidence. No hardware."""
from pathlib import Path
import hashlib,json,struct,subprocess
root=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
def baseline(path):return subprocess.check_output(['git','show','dcfff9e:'+path],cwd=root)
def oldtext(path):return baseline(path).decode('utf-8').replace('\r\n','\n')
def current(path):return (root/path).read_text(encoding='utf-8').replace('\r\n','\n')
def part(text,start,end):return text[text.index(start):text.index(end,text.index(start))].strip()
cpp='src/platform/windows/AmiBiosDumpWindows.cpp';header='src/platform/common/BiosImageValidation.h'
old=oldtext(cpp);new=current(cpp);preserved={}
pairs=[('fixed_vendor_runner',part(old,'enum class ReadCommand','\nstd::string shapeJson'),part(new,'enum class ReadCommand','\nvoid verifyFpt19ReadOutput')),
       ('signature_check',part(old,'LONG signature(','\nstd::string serviceState'),part(new,'LONG signature(','\nstd::string serviceState')),
       ('afu_and_fpt16_specs',part(old,'constexpr VendorTool afu','\nconstexpr wchar_t biosKey'),part(new,'constexpr VendorTool afu','\nconstexpr VendorTool fpt19')),
       ('image_validator',part(oldtext(header),'inline Shape validate(','\ninline bool allRegionsReadable'),part(current(header),'inline Shape validate(','\ninline bool allRegionsReadable'))]
for name,a,b in pairs:
    assert a==b,name
    preserved[name]=dict(byte_equal_source=True,sha256=sha(a.encode()))
gui_diff=subprocess.check_output(['git','diff','--name-only','dcfff9e','--','src/gui'],cwd=root).decode().strip()
assert not gui_diff
preserved['gui_sources_unchanged']=True
runtime=[]
for group in ['AmiAfu','IntelFpt']:
    for path in sorted((root/'src/platform/windows/runtime'/group).iterdir()):
        if not path.is_file():continue
        relative=path.relative_to(root).as_posix();data=path.read_bytes();original=baseline(relative)
        assert data.replace(b'\r\n',b'\n')==original.replace(b'\r\n',b'\n') if path.suffix in ['.txt','.json'] else data==original
        runtime.append(dict(file=relative,sha256=sha(data),unchanged=True))
def table(path,start,count):
    data=path.read_bytes();rows=[]
    for n in range(count):
        at=start+n*208;low,high=struct.unpack_from('<HH',data,at)
        rows.append(dict(index=n,low=hex(low),high=hex(high),name=data[at+4:at+204].split(b'\0')[0].decode('ascii',errors='replace'),
                         internal=struct.unpack_from('<I',data,at+204)[0],matches_gm6_7a0c=low<=0x7a0c<=high))
    return rows
fpt16=table(root/'src/platform/windows/runtime/IntelFpt/FPTW64.exe',0x172ec0,31)
fpt19=table(root/'src/platform/windows/runtime/IntelFpt19/FPTW64.exe',0x29eb60,13)
assert any(r['matches_gm6_7a0c'] for r in fpt16) and not any(r['matches_gm6_7a0c'] for r in fpt19)
honor=root/'research/devices/hlyl-wxx9/honor-20261018/firmware/HLYL-WXX9_BIOS_1.09_ORIGINAL_16MB.bin'
h=honor.read_bytes();assert len(h)==16777216 and sha(h)=='38597994dc39ed17d57cee8331ac107579b6ff186934cdbc873d3c5f363a806e'
hr=honor.with_name(honor.name+'.report.txt').read_text(encoding='utf-8',errors='replace')
honor_evidence=dict(bytes=len(h),sha256=sha(h),fdm_signature_at_ea8000=h[0xea8000:0xea8010].hex(),
    parser_has_insyde_fdm='Insyde H2O FlashDeviceMap' in hr,parser_has_insyde_fdc='Insyde FDC store' in hr,
    h2offt_g_read_claim_source='README_RU.md; original invocation log not found in reviewed firmware folder',
    physical_spi_complete_proven=False)
assert honor_evidence['parser_has_insyde_fdm'] and honor_evidence['parser_has_insyde_fdc']
folder=root/'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/bios-dumps/bios-20261024-193321-0'
images=[(folder/name).read_bytes() for name in ['bios.bin','bios-verify.bin']]
assert len(images[0])==33554432 and images[0]==images[1]
tool=json.loads((folder/'tool.json').read_text(encoding='utf-8-sig'))
wujie=dict(bytes=len(images[0]),sha256=sha(images[0]),two_files_equal=True,tool=tool)
result=dict(baseline='dcfff9e / 0.1.190',hardware_executed=False,application_executed=False,source_preservation=preserved,
    legacy_runtime_preserved=runtime,fpt16_table=fpt16,fpt19_table=fpt19,honor=honor_evidence,wujie=wujie)
(out/'audit-evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(source_preservation=preserved,legacy_resources=len(runtime),fpt16_gm6=True,fpt19_gm6=False,honor=honor_evidence,wujie=dict(bytes=wujie['bytes'],sha256=wujie['sha256'],two_files_equal=True)),ensure_ascii=False,indent=2))
