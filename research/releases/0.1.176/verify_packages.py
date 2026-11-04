"""Inspect local release archives and PE resources only; never launch the product."""
from pathlib import Path
import ast, hashlib, json, re, struct, subprocess, tarfile, zipfile

ROOT = Path(__file__).resolve().parents[3]
RELEASES = ROOT/'GitHub_Releases'
VERSION = '0.1.176'
APP_REVISION = '5a87f5a55d5d77605602d9512b1946f4636b0765'
sha = lambda data: hashlib.sha256(data).hexdigest()
state = json.loads((ROOT/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert state['status'] == 'complete' and state['version'] == VERSION
revision = APP_REVISION
sums = dict(line.split('  ',1)[::-1] for line in (RELEASES/'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines())
old_sums = subprocess.check_output(['git','show','HEAD:GitHub_Releases/SHA256SUMS.txt'],cwd=ROOT,text=True)
assert all(sums[name] == digest for digest,name in (line.split('  ',1) for line in old_sums.splitlines()))
report = {'version':VERSION,'source_revision':revision,'application_executed':False,
          'drivers_installed':False,'application_tests_run':False,'published':False,'packages':[]}

# Reuse only the existing pure PE-resource parser, without executing its old audit.
source = ROOT/'research/packaging/embedded-runtime-20261021/verify-delivery.py'
function = next(node for node in ast.parse(source.read_text(encoding='utf-8-sig')).body
                if isinstance(node,ast.FunctionDef) and node.name == 'resources')
exec(compile(ast.Module(body=[function],type_ignores=[]),str(source),'exec'))
catalog = re.findall(r'\{(\d+),"([^"]+)","([^"]+)","([a-f0-9]{64})",(\d+)\}',
    (ROOT/'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text())
assert len(catalog) == 28
embedded = []
for platform, suffix in (('windows','.zip'),('linux','.tar.gz')):
    assert state['platforms'][platform]['status'] == 'verified'
    folder = RELEASES/f'MagicControl-{VERSION}-{platform}-x64-portable'
    archive = folder.with_name(folder.name+suffix)
    manifest = json.loads((folder/'package-manifest.json').read_text())
    info = json.loads((folder/'build-info.json').read_text())
    assert manifest['product'] == info['product'] == 'MagicControl'
    assert manifest['version'] == info['version'] == VERSION
    assert manifest['platform'] == info['platform'] == platform
    assert info['source_revision'] == revision
    assert not info['source_revision_is_base'] and not info['includes_uncommitted_source_changes']
    assert not info['magiccontrol_source_included']
    assert info['source_binary_sha256'] == state['platforms'][platform]['sha256'].lower()
    actual = {p.relative_to(folder).as_posix():p for p in folder.rglob('*') if p.is_file()}
    expected = {item['path']:item for item in manifest['files']}
    assert len(expected) == len(manifest['files'])
    assert set(actual) == set(expected)|{'package-manifest.json'}
    for name,item in expected.items():
        data = actual[name].read_bytes()
        assert len(data) == item['bytes'] and sha(data) == item['sha256'],name
        parts = Path(name).parts
        assert not any(p.lower() in ('src','research','build','logs','backups','.ssh') or p.lower().startswith('logs_') for p in parts[:-1])
        assert parts[0].lower() not in ('drivers','pawnio')
        assert Path(name).name.lower() not in ('mctl','mctl.exe','device-settings.ini','keyboard-cycle.ini','battery-enforcement-platform.txt','launch-handoff.json','bios-inventory.json','id_rsa','id_ed25519')
        assert not name.lower().endswith(('.pdb','.cpp','.h','.hpp','.cs'))
    for name in ('app-settings.ini','fan-curves.csv'):
        assert (folder/'settings'/name).read_bytes() == (ROOT/'packaging'/name).read_bytes()
    assert set(p.relative_to(folder/'settings').as_posix() for p in (folder/'settings').rglob('*') if p.is_file()) == {'app-settings.ini','fan-curves.csv'}
    required = {'LICENSE.txt','THIRD-PARTY-NOTICES.txt','fonts/NotoSansSC.ttf','fonts/NotoSansTC.ttf','fonts/OFL-NotoSansSC.txt','fonts/OFL-NotoSansTC.txt'}
    assert required <= set(actual)
    for font in ('NotoSansSC.ttf','NotoSansTC.ttf'):
        assert (folder/'fonts'/font).read_bytes() == (ROOT/'src/gui/assets/fonts'/font).read_bytes()
    if platform == 'windows':
        assert (folder/'MagicControl.exe').read_bytes() == (ROOT/'MagicControl/windows/MagicControl.exe').read_bytes()
        assert {'licenses/ADLX-SDK-LICENSE.txt','licenses/IGCL-SDK-LICENSE.txt','SOURCE_OFFER.txt'} <= set(actual)
        assert not (folder/'PawnIO').exists() and not (folder/'drivers').exists()
        found = resources(folder/'MagicControl.exe')
        for rid,group,name,digest,size in catalog:
            original = ROOT/'src/platform/windows/runtime'/('PawnIO' if group=='Installers' else group)/name
            payloads = [data for key,data in found.items() if len(key)==3 and key[:2] == (10,int(rid))]
            assert payloads and all(len(data)==int(size) and sha(data)==digest and data==original.read_bytes() for data in payloads)
            embedded.append({'id':int(rid),'group':group,'name':name,'bytes':int(size),'sha256':digest,'matches_original':True})
        with zipfile.ZipFile(archive) as packed:
            assert packed.testzip() is None
            assert len(packed.namelist()) == len(set(packed.namelist()))
            content = {name.removeprefix(folder.name+'/'):packed.read(name) for name in packed.namelist()}
    else:
        assert info['linux_symbols_stripped']
        assert {'linux-abi.txt','start.sh','libgtk4-layer-shell.so.0','licenses/DRM-UAPI-NOTICES.txt'} <= set(actual)
        assert b'\r' not in (folder/'start.sh').read_bytes()
        # ELF64 section-name inspection proves strip removed the full symbol table.
        data = (folder/'MagicControl').read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01'
        offset = struct.unpack_from('<Q',data,40)[0]
        size,count,strings = struct.unpack_from('<HHH',data,58)
        start,length = struct.unpack_from('<QQ',data,offset+size*strings+24)
        names = data[start:start+length]
        assert b'.symtab\0' not in names
        with tarfile.open(archive) as packed:
            entries = packed.getmembers()
            assert len(entries) == len({m.name for m in entries})
            assert all(m.isfile() or m.isdir() for m in entries)
            content = {m.name.removeprefix(folder.name+'/'):packed.extractfile(m).read() for m in entries if m.isfile()}
            for m in entries:
                if m.isfile(): assert m.mode == (0o755 if Path(m.name).name in ('MagicControl','start.sh') else 0o644)
    assert set(content) == set(actual)
    assert all(content[name] == path.read_bytes() for name,path in actual.items())
    digest = sha(archive.read_bytes())
    assert sums[archive.name] == digest
    report['packages'].append({'platform':platform,'archive':archive.name,'bytes':archive.stat().st_size,
        'sha256':digest,'files':len(actual),'build_info':info,'manifest_verified':True,
        'archive_bytes_verified':True,'clean_defaults_verified':True})
report['embedded_resources'] = embedded
# A checkout must preserve the same bytes that the package manifest describes.
roots = [f'GitHub_Releases/MagicControl-{VERSION}-{p}-x64-portable' for p in ('windows','linux')]
roots += [str(Path('GitHub_Releases')/p['archive']) for p in report['packages']]
index = subprocess.check_output(['git','ls-files','-s','-z','--',*roots],cwd=ROOT)
indexed = 0
for entry in filter(None,index.split(b'\0')):
    meta,name = entry.split(b'\t',1)
    expected = meta.split()[1].decode()
    data = (ROOT/name.decode('utf-8')).read_bytes()
    assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest() == expected,name
    indexed += 1
assert indexed == sum(p['files'] for p in report['packages'])+len(report['packages'])
report['git_index_bytes_verified'] = indexed
(RELEASES/f'VERIFICATION-{VERSION}.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(RELEASES/f'SHA256SUMS-{VERSION}.txt').write_text(''.join(f"{p['sha256']}  {p['archive']}\n" for p in report['packages']),encoding='utf-8')
print(json.dumps({'version':VERSION,'source_revision':revision,'packages':report['packages'],'embedded_members_verified':len(embedded)},ensure_ascii=False,indent=2))
