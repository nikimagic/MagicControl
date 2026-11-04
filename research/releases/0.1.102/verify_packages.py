"""Read-only release artifact verification; does not run MagicControl."""
from pathlib import Path
import hashlib, json, re, subprocess, tarfile, zipfile

ROOT = Path(__file__).resolve().parents[3]
RELEASES = ROOT / 'GitHub_Releases'
VERSION = '0.1.102'
def sha(data): return hashlib.sha256(data).hexdigest()
session = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
assert session['status'] == 'complete' and session['version'] == VERSION
assert all(session['platforms'][p]['status'] == 'verified' for p in ('windows', 'linux'))
sums = dict(line.split('  ', 1)[::-1] for line in (RELEASES / 'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines())
report = {'version': VERSION, 'application_executed': False, 'drivers_installed': False, 'packages': []}
revisions = set()
for platform, suffix in [('windows', '.zip'), ('linux', '.tar.gz')]:
    folder = RELEASES / f'MagicControl-{VERSION}-{platform}-x64-portable'
    archive = folder.with_name(folder.name + suffix)
    manifest = json.loads((folder / 'package-manifest.json').read_text())
    info = json.loads((folder / 'build-info.json').read_text())
    revisions.add(info['source_revision'])
    assert manifest['version'] == info['version'] == VERSION
    assert manifest['product'] == info['product'] == 'MagicControl'
    assert manifest['platform'] == info['platform'] == platform
    assert info['magiccontrol_source_included'] is False
    assert info['includes_uncommitted_source_changes'] is False
    assert info['source_revision_is_base'] is False
    assert info['source_binary_sha256'] == session['platforms'][platform]['sha256'].lower()
    expected = {e['path']: e for e in manifest['files']}
    actual = {p.relative_to(folder).as_posix(): p for p in folder.rglob('*') if p.is_file()}
    assert set(actual) == set(expected) | {'package-manifest.json'}
    for name, entry in expected.items():
        data = actual[name].read_bytes()
        assert len(data) == entry['bytes'] and sha(data) == entry['sha256'], name
        parts = Path(name).parts
        assert not any(p.lower() in ('src', 'research', 'build', 'logs', 'backups', '.ssh') or p.lower().startswith('logs_') for p in parts)
        assert Path(name).name.lower() not in ('mctl', 'mctl.exe', 'device-settings.ini', 'keyboard-cycle.ini', 'battery-enforcement-platform.txt', 'id_rsa', 'id_ed25519')
        assert not name.lower().endswith(('.pdb', '.cpp', '.h', '.hpp', '.cs'))
    for name in ('app-settings.ini', 'fan-curves.csv'):
        assert (folder / name).read_bytes() == (ROOT / 'packaging' / name).read_bytes()
    for name in ('LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'fonts/NotoSansSC.ttf', 'fonts/NotoSansTC.ttf', 'fonts/OFL-NotoSansSC.txt', 'fonts/OFL-NotoSansTC.txt'):
        assert name in actual
    for font, expected_sha in [('NotoSansSC','a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da'),('NotoSansTC','864727d210d54f2537bbe23b3a839436c3992af72de9322af5270897246bd44f')]:
        assert sha((folder / 'fonts' / (font+'.ttf')).read_bytes()) == expected_sha
    assert 'Noto Sans TC' in (folder / 'THIRD-PARTY-NOTICES.txt').read_text()
    assert VERSION in (folder / 'README.txt').read_text()
    if platform == 'windows':
        assert (folder / 'MagicControl.exe').read_bytes() == (ROOT / 'MagicControl/windows/MagicControl.exe').read_bytes()
        assert (folder / 'MagicControl.exe').read_bytes() == (ROOT / 'build/windows/bin/Release/MagicControl.exe').read_bytes()
        required = re.search(r'set\(MAGICCONTROL_PAWNIO_REQUIRED_FILES(.*?)\)', (ROOT / 'cmake/PawnIORuntime.cmake').read_text(), re.S).group(1).split()
        for name in required:
            assert (folder / 'PawnIO' / name).read_bytes() == (ROOT / 'src/platform/windows/runtime/PawnIO' / name).read_bytes()
        catalogue = json.loads((folder / 'drivers/Uniwill/PACKAGES.json').read_text())
        for package in catalogue['packages']:
            base = folder / 'drivers/Uniwill' / package['directory']
            proof = json.loads((base / 'PROVENANCE.json').read_text())
            for entry in proof['files']:
                data = (base / entry['path']).read_bytes()
                assert len(data) == entry['bytes'] and sha(data) == entry['sha256']
        with zipfile.ZipFile(archive) as packed:
            assert packed.testzip() is None
            assert len(packed.namelist()) == len(set(packed.namelist()))
            content = {name.removeprefix(folder.name + '/'): packed.read(name) for name in packed.namelist()}
        assert {'licenses/ADLX-SDK-LICENSE.txt', 'licenses/AMD-ADL-MIT.txt', 'licenses/IGCL-SDK-LICENSE.txt', 'SOURCE_OFFER.txt'} <= set(actual)
    else:
        assert info['linux_symbols_stripped'] is True
        assert {'linux-abi.txt', 'start.sh', 'libgtk4-layer-shell.so.0', 'licenses/DRM-UAPI-NOTICES.txt'} <= set(actual)
        assert b'\r' not in (folder / 'start.sh').read_bytes()
        with tarfile.open(archive) as packed:
            entries = packed.getmembers()
            assert not any(m.issym() or m.islnk() for m in entries)
            assert len([m.name for m in entries]) == len(set(m.name for m in entries))
            content = {m.name.removeprefix(folder.name + '/'): packed.extractfile(m).read() for m in entries if m.isfile()}
            for member in entries:
                if member.isfile():
                    assert member.mode == (0o755 if Path(member.name).name in ('MagicControl', 'start.sh') else 0o644)
    assert set(content) == set(actual)
    for name, data in content.items(): assert data == actual[name].read_bytes(), name
    digest = sha(archive.read_bytes())
    assert sums[archive.name] == digest
    report['packages'].append({'platform': platform, 'archive': archive.name, 'bytes': archive.stat().st_size, 'sha256': digest, 'files': len(actual), 'build_info': info, 'manifest_verified': True, 'archive_bytes_verified': True, 'clean_defaults_verified': True})
assert len(revisions) == 1
report['source_revision'] = next(iter(revisions))
report['old_checksum_entries_preserved'] = True
old_sums = subprocess.check_output(['git', 'show', 'HEAD:GitHub_Releases/SHA256SUMS.txt'], cwd=ROOT, text=True)
for line in old_sums.splitlines():
    digest, name = line.split('  ', 1)
    assert sums[name] == digest
(Path(__file__).parent / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))
