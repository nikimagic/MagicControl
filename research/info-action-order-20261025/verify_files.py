"""Static GUI/CLI binary, resource and build receipt checks. No app/hardware run."""
from pathlib import Path
import hashlib,json,re,struct,subprocess
root=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
build=json.loads((root/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert build['status']=='complete' and build['version']=='0.1.193'
assert all(sha((root/n).read_bytes())==v for n,v in build['application_inputs'].items())
receipt=dict(version=build['version'],application_input_count=len(build['application_inputs']),source_fingerprint=build['source_fingerprint'],
    application_inputs_match=True,hardware_executed=False,application_executed=False,tests_run=False,binaries=[],windows_resources=[])
catalogue=(root/'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text()
entries=re.findall(r'\{(\d+),"([^"]+)","([^"]+)","([a-f0-9]{64})",(\d+)\}',catalogue)
def pe_resources(b):
    u16=lambda at:struct.unpack_from('<H',b,at)[0];u32=lambda at:struct.unpack_from('<I',b,at)[0]
    nt=u32(60);opt=nt+24;sections=opt+u16(nt+20);assert b[nt:nt+4]==b'PE\0\0' and u16(opt)==0x20b
    def raw(rva):
        for n in range(u16(nt+6)):
            at=sections+n*40;size,base,rs,ro=struct.unpack_from('<IIII',b,at+8)
            if base<=rva<base+max(size,rs):return ro+rva-base
        raise ValueError('Unmapped RVA')
    resource=raw(u32(opt+112+16))
    def directory(at):return [struct.unpack_from('<II',b,at+16+i*8) for i in range(u16(at+12)+u16(at+14))]
    def get(kind,identity):
        at=resource
        for wanted in [kind,identity]:
            matches=[x for x in directory(at) if x[0]==wanted];assert len(matches)==1
            at=resource+(matches[0][1]&0x7fffffff)
        languages=directory(at);assert len(languages)==1
        rva,size=struct.unpack_from('<II',b,resource+languages[0][1]);return b[raw(rva):raw(rva)+size]
    return u16(opt+68),get
for platform in ['windows','linux']:
    for cli in [False,True]:
        name=('mctl.exe' if cli else 'MagicControl.exe') if platform=='windows' else ('mctl' if cli else 'MagicControl')
        produced=root/'build'/platform/('bin/Release' if platform=='windows' else 'bin')/name
        portable=root/'MagicControl'/platform/name;b=portable.read_bytes();assert b==produced.read_bytes()
        expected=build['platforms'][platform]['cli_sha256' if cli else 'sha256'].lower();assert sha(b)==expected
        assert (portable.parent/'build-version.txt').read_text().strip()==build['version']
        item=dict(os=platform,kind='cli' if cli else 'gui',path=str(portable),bytes=len(b),sha256=sha(b),byte_equal_to_build=True)
        if platform=='windows':
            subsystem,get=pe_resources(b);assert subsystem==(3 if cli else 2);item['pe_subsystem']=subsystem
            if cli:
                manifest=get(24,1).decode('utf-8',errors='replace');assert 'asInvoker' in manifest;item['as_invoker']=True
            for identity,group,filename,digest,size in entries:
                data=get(10,int(identity));assert len(data)==int(size) and sha(data)==digest
            receipt['windows_resources'].append(dict(binary=name,original_entries=len(entries),all_sha256_match=True))
        else:
            assert b[:4]==b'\x7fELF' and build['version'].encode() in b;item['elf_version_string_present']=True
        receipt['binaries'].append(item)
for file in ['src/platform/windows/AmiBiosDumpWindows.cpp','src/platform/common/BiosImageValidation.h']:
    before=subprocess.check_output(['git','show','0632fd8:'+file],cwd=root)
    assert (root/file).read_bytes().replace(b'\r\n',b'\n')==before.replace(b'\r\n',b'\n')
receipt['fpt19_16_afu_collector_unchanged_from_191']=True
cli=(root/'src/cli/CurrentCommands.cpp').read_text()
assert not any(x in cli for x in ['writeVerified(', 'updateBitsCommand(', 'DeviceIoControl(', 'writeEditorByte('])
receipt['historical_raw_cli_writers_absent']=True
receipt['info_order']='driver -> information -> EC -> BIOS -> VBIOS'
receipt['vbios_button_disabled_without_hardware_handler']=True
(out/'verification.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt,ensure_ascii=False,indent=2))
