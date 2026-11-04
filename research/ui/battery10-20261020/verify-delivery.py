"""Inspect saved artifacts and checksums only; no app, hardware or tests run."""
from pathlib import Path
import hashlib
import json

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[2]

def read(name):
    return json.loads((OUT / name).read_text(encoding='utf-8-sig'))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

session = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
if session['status'] != 'complete':
    raise RuntimeError('Common build is not complete')
inputs = session['application_inputs']
changed = [path for path, digest in inputs.items() if sha(ROOT / path) != digest.lower()]
personal = [{'path': path, 'unchanged': sha(ROOT / 'MagicControl' / path) == digest.lower()}
            for path, digest in read('personal-before-staging.json').items()]
protected = [{'path': path, 'unchanged': sha(ROOT / path) == digest.lower()}
             for path, digest in read('baseline.json')['protected'].items()]
artifacts = []
for platform, names in [('windows', ('MagicControl.exe', 'mctl.exe')), ('linux', ('MagicControl', 'mctl'))]:
    folder = ROOT / 'MagicControl' / platform
    build = ROOT / 'build' / platform / 'bin'
    if platform == 'windows':
        build /= 'Release'
    if (folder / 'build-version.txt').read_text(encoding='utf-8-sig').strip() != session['version']:
        raise RuntimeError('Portable version mismatch: ' + platform)
    for name in names:
        artifacts.append({'platform': platform, 'name': name, 'sha256': sha(folder / name),
                          'matches_build': sha(folder / name) == sha(build / name)})
if changed or not all(p['unchanged'] for p in personal + protected) or not all(p['matches_build'] for p in artifacts):
    raise RuntimeError('Artifact/source/personal/protected checksum mismatch')
report = {'version': session['version'], 'status': session['status'],
          'source_fingerprint': session['source_fingerprint'], 'input_count': len(inputs),
          'input_mismatches': changed, 'personal_files': personal, 'protected_ui_files': protected,
          'artifacts': artifacts, 'upper_buttons': [100, 95, 85, 10], 'wujie_restart': 80,
          'ten_percent_scope': 'Wujie Windows only; 10/80 crossed pair is explicitly requested, enforcement unverified',
          'scope': 'Compilation and saved artifact/source inspection only; no tests, GUI or hardware run.'}
(OUT / ('verification-' + session['version'] + '.json')).write_text(
    json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'version': session['version'], 'inputs_match': len(inputs),
                  'personal_preserved': len(personal), 'protected_unchanged': len(protected),
                  'binaries_verified': len(artifacts)}))
