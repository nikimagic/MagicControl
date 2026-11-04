"""Static source/build/resource checks only. No product or hardware execution."""
from pathlib import Path
import ast
import hashlib
import json
import re
import struct
import subprocess

root=Path(__file__).resolve().parents[2]
out=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
build=json.loads((root/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert build['version']=='0.1.198' and build['status']=='complete'
assert all(sha((root/p).read_bytes())==digest for p,digest in build['application_inputs'].items())
# Reuse only the pure PE decoder function from the preceding file checker.
# Do not execute that script's top-level assertions, subprocesses or writes.
previous=root/'research/firmware-validator-tests-20261025/verify_release.py'
tree=ast.parse(previous.read_text())
function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='pe_resources')
exec(compile(ast.Module(body=[function],type_ignores=[]),str(previous),'exec'),globals())
catalogue=(root/'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text()
entries=re.findall(r'\{(\d+),"([^"]+)","([^"]+)","([a-f0-9]{64})",(\d+)\}',catalogue)
assert len(entries)==47
receipt={'version':build['version'],'application_inputs_match':True,'application_input_count':len(build['application_inputs']),
    'hardware_executed':False,'application_executed':False,'vendor_executed':False,'new_tests_run':False,'binaries':[]}
for os in ['windows','linux']:
    for cli in [False,True]:
        name=('mctl.exe' if cli else 'MagicControl.exe') if os=='windows' else ('mctl' if cli else 'MagicControl')
        portable=root/'MagicControl'/os/name
        built=root/'build'/os/('bin/Release' if os=='windows' else 'bin')/name
        b=portable.read_bytes();assert b==built.read_bytes()
        assert sha(b)==build['platforms'][os]['cli_sha256' if cli else 'sha256'].lower()
        assert (portable.parent/'build-version.txt').read_text().strip()==build['version']
        item={'os':os,'cli':cli,'bytes':len(b),'sha256':sha(b)}
        if os=='windows':
            subsystem,get=pe_resources(b);assert subsystem==(3 if cli else 2)
            if cli:assert 'asInvoker' in get(24,1).decode('utf-8',errors='replace')
            for identity,group,filename,digest,size in entries:
                data=get(10,int(identity));assert len(data)==int(size) and sha(data)==digest
            item['original_resources_verified']=len(entries)
        else:assert b[:4]==b'\x7fELF' and build['version'].encode() in b
        receipt['binaries'].append(item)
protected=['src/gui/common/Geometry.h','src/gui/common/DeviceHeader.h',
    'src/gui/windows/BiosPage.cpp','src/platform/windows/AmiBiosDumpWindows.cpp',
    'src/platform/windows/NvflashWindows.cpp','src/platform/windows/uniwill-acpi/BackendWindows.cpp',
    'src/platform/linux/NvflashLinux.cpp','src/platform/common/VbiosRomDump.cpp',
    'src/platform/common/BiosImageValidation.h','src/platform/common/VbiosImageValidation.h',
    'src/platform/common/NvflashPlatform.h','src/platform/common/NvflashOutput.h']
for p in protected:
    before=subprocess.check_output(['git','show','bf92d5f:'+p],cwd=root)
    assert before.replace(b'\r\n',b'\n')==(root/p).read_bytes().replace(b'\r\n',b'\n'),p
receipt['protected_sources_unchanged_from_197']=protected
native=(root/'src/platform/windows/RomIdWindows.cpp').read_text()
for forbidden in ['->write(', 'writeVerified(', 'updateBits', 'createWindowsAcpiFactoryBackend(', 'StartService(', 'CreateService(']:
    assert forbidden not in native
assert 'createWindowsAcpiDiagnosticBackend()' in native and '0x770+i' in native and 'read(0x7C3,result.platform)' in native
presentation=(root/'src/gui/common/InventoryPresentation.h').read_text()
assert 'title==mc::kEcIdentityInventoryMetadata' in presentation and 'ensureInventoryEcIdentityRows(visible)' in presentation
linux=(root/'src/gui/linux/BiosPage.cpp').read_text()
assert 'state->osd=osd;collect(*state)' not in linux and 'InventoryCache::load()' in linux and 'InventoryCache::save(state.inventory,error)' in linux
receipt['readonly_factory_only']=True
receipt['shared_cache_projection_and_explicit_collection_wired']=True
(out/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print('PASS0.1.198: four portable versions/SHA, all application inputs,47original resources per Windows binary; reader/geometry sources unchanged.')
