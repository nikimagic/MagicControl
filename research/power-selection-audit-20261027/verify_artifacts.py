"""Offline artifact verification; never executes product/vendor binaries."""
from pathlib import Path
import hashlib
import json
import tarfile
import zipfile
from datetime import datetime, timezone

root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
session = json.loads((root/'build/product-build.json').read_text(encoding='utf-8-sig'))
version = session['version']
assert version == '0.1.244' and session['status'] == 'complete'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
result = dict(version=version, verified_utc=datetime.now(timezone.utc).isoformat(),
              published=False, hardware_executed=False, application_executed=False,
              tests_run=False, source_fingerprint=session['source_fingerprint'],
              binaries=[], assets=[])
for platform in ('windows', 'linux'):
    assert session['platforms'][platform]['status'] == 'verified'
    portable = root/'MagicControl'/platform
    assert (portable/'build-version.txt').read_text(encoding='utf-8-sig').strip() == version
    for name, key in (('MagicControl', 'sha256'), ('mctl', 'cli_sha256')):
        filename = name + ('.exe' if platform == 'windows' else '')
        path = portable/filename
        built = root/'build'/platform/'bin'
        if platform == 'windows': built /= 'Release'
        digest = sha(path)
        assert digest == sha(built/filename) == session['platforms'][platform][key].lower()
        result['binaries'].append(dict(platform=platform, binary=filename,
            sha256=digest, bytes=path.stat().st_size, actual_portable_matches_build=True))
    suffix = '.zip' if platform == 'windows' else '.tar.gz'
    archive = root/'GitHub_Releases'/f'MagicControl-{version}-{platform}-x64-portable{suffix}'
    if platform == 'windows':
        with zipfile.ZipFile(archive) as z: names = z.namelist()
    else:
        with tarfile.open(archive) as t: names = [m.name for m in t.getmembers() if m.isfile()]
    assert names
    forbidden = {'mctl', 'mctl.exe', 'rom_id', 'platform_id', 'project_id',
                 'bios-inventory.json', 'device-settings.ini', 'battery-enforcement-platform.txt'}
    for name in names:
        parts = Path(name).parts
        assert Path(name).name not in forbidden
        assert not any(p.lower() == 'logs' or p.lower().startswith('logs_') for p in parts)
    result['assets'].append(dict(platform=platform, archive=archive.name,
        bytes=archive.stat().st_size, sha256=sha(archive), files=len(names),
        cli_absent=True, private_data_absent=True,
        archive_bytes_verified_by='Tools/Package-Releases.py'))
(out/'VERIFICATION.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
release = root/'GitHub_Releases'
(release/f'VERIFICATION-{version}.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
(release/f'SHA256SUMS-{version}.txt').write_text(''.join(
    a['sha256']+'  '+a['archive']+'\n' for a in result['assets']), encoding='utf-8')
print(json.dumps(result, indent=2))
