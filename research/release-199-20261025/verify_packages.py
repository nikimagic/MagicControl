"""Verify local GUI releases as files; never execute applications or readers."""
from pathlib import Path, PurePosixPath
import ast
import hashlib
import json
import re
import struct
import tarfile
import zipfile

root=Path(__file__).resolve().parents[2];releases=root/'GitHub_Releases'
sha=lambda b:hashlib.sha256(b).hexdigest()
version='0.1.199';build=json.loads((root/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert build['status']=='complete' and build['version']==version
assert all(sha((root/p).read_bytes())==v for p,v in build['application_inputs'].items())
tree=ast.parse((root/'research/firmware-validator-tests-20261025/verify_release.py').read_text())
function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='pe_resources')
exec(compile(ast.Module(body=[function],type_ignores=[]),'existing pure PE decoder','exec'),globals())
catalogue=(root/'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text()
resources=re.findall(r'\{(\d+),"([^"]+)","([^"]+)","([a-f0-9]{64})",(\d+)\}',catalogue)
assert len(resources)==47
receipt={'version':version,'published':False,'hardware_executed':False,'application_executed':False,
         'vendor_executed':False,'cli_in_release':False,'assets':[],'personal_cli_preserved':[]}
for os in ['windows','linux']:
    stem=f'MagicControl-{version}-{os}-x64-portable';folder=releases/stem
    info=json.loads((folder/'build-info.json').read_text())
    manifest=json.loads((folder/'package-manifest.json').read_text())
    assert info['version']==manifest['version']==version and info['platform']==manifest['platform']==os
    assert info['product']==manifest['product']=='MagicControl' and info['cli_included'] is False
    assert info['source_revision'].startswith('e01208f') and not info['includes_uncommitted_source_changes']
    expected={f['path']:f for f in manifest['files']}
    actual={p.relative_to(folder).as_posix():p for p in folder.rglob('*') if p.is_file()}
    assert set(actual)==set(expected)|{'package-manifest.json'}
    for name,p in actual.items():
        assert not p.is_symlink()
        path=PurePosixPath(name)
        assert not path.is_absolute() and '..' not in path.parts and '\\' not in name
        assert path.name not in ['mctl','mctl.exe','rom_id','platform_id','bios-inventory.json','device-settings.ini','battery-enforcement-platform.txt']
        assert not any(x=='logs' or x.startswith('logs_') for x in path.parts)
        if name in expected:assert len(p.read_bytes())==expected[name]['bytes'] and sha(p.read_bytes())==expected[name]['sha256']
    for name in ['fan-curves.csv','app-settings.ini']:
        assert (folder/'settings'/name).read_bytes()==(root/'packaging'/name).read_bytes()
    exe='MagicControl.exe' if os=='windows' else 'MagicControl'
    source=root/'MagicControl'/os/exe
    assert info['source_binary_sha256']==sha(source.read_bytes())==build['platforms'][os]['sha256'].lower()
    archive=folder.with_name(stem+('.zip' if os=='windows' else '.tar.gz'))
    if os=='windows':
        assert (folder/exe).read_bytes()==source.read_bytes()
        subsystem,get=pe_resources((folder/exe).read_bytes());assert subsystem==2
        for identity,group,filename,digest,size in resources:
            b=get(10,int(identity));assert len(b)==int(size) and sha(b)==digest
        with zipfile.ZipFile(archive) as z:
            assert {n[len(stem)+1:] for n in z.namelist()}==set(actual)
            for name,p in actual.items():assert z.read(stem+'/'+name)==p.read_bytes()
    else:
        assert (folder/exe).read_bytes()[:4]==b'\x7fELF' and version.encode() in (folder/exe).read_bytes()
        nvflash=folder/'tools/NvFlash/nvflash'
        assert sha(nvflash.read_bytes())=='754693fb8f833be75601f95cb08a6e0cf538053cce84ab69020ec74ae96a6e70'
        for name in ['NOTICE.txt','THIRD-PARTY-NOTICES.txt','PROVENANCE.json']:
            assert (folder/'licenses/NVFlash'/name).read_bytes()==(root/'src/platform/linux/runtime/NvFlash'/name).read_bytes()
        with tarfile.open(archive) as t:
            members={m.name[len(stem)+1:]:m for m in t.getmembers() if m.isfile()}
            assert set(members)==set(actual)
            assert all(m.isfile() or m.isdir() for m in t.getmembers())
            for name,m in members.items():assert t.extractfile(m).read()==actual[name].read_bytes()
            for name in ['MagicControl','start.sh','tools/NvFlash/nvflash']:assert members[name].mode&0o111==0o111
    assert archive.stat().st_size<128*1024*1024
    receipt['assets'].append({'platform':os,'archive':archive.name,'bytes':archive.stat().st_size,'sha256':sha(archive.read_bytes()),
        'files':len(actual),'manifest_verified':True,'cli_absent':True,'private_data_absent':True,'source_revision':info['source_revision']})
    private=root/'MagicControl'/os/('mctl.exe' if os=='windows' else 'mctl')
    assert sha(private.read_bytes())==build['platforms'][os]['cli_sha256'].lower()
    receipt['personal_cli_preserved'].append({'platform':os,'version':version,'sha256':sha(private.read_bytes())})
sums={line.split('  ',1)[1]:line.split('  ',1)[0] for line in (releases/'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines()}
before={line.split('  ',1)[1]:line.split('  ',1)[0] for line in (root/'research/release-199-20261025/baseline/SHA256SUMS.txt.worktree').read_text(encoding='utf-8-sig').splitlines()}
assert all(sums[k]==v for k,v in before.items())
assert all(sums[a['archive']]==a['sha256'] for a in receipt['assets'])
(releases/f'SHA256SUMS-{version}.txt').write_text(''.join(a['sha256']+'  '+a['archive']+'\n' for a in receipt['assets']),encoding='utf-8')
publication=releases/f'PUBLICATION-{version}.json'
if publication.exists():
    published=json.loads(publication.read_text())
    assert published['version']==version and not published['draft']
    for a in receipt['assets']:
        remote=next(x for x in published['assets'] if x['name']==a['archive'])
        assert remote['bytes']==a['bytes'] and remote['sha256']==a['sha256']
    receipt.update(published=True,publication_record=publication.name,github_assets_verified=True)
(releases/f'VERIFICATION-{version}.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt,indent=2))
