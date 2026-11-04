"""Read-only source/artifact/settings inspection; does not run app or tests."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()
before = json.loads((OUT / 'before.json').read_text(encoding='utf-8'))
state = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
inputs = [{'path': p, 'matches': sha(ROOT / p) == h.lower()} for p, h in state['application_inputs'].items()]
personal = [{'path': p['path'], 'unchanged': sha(ROOT / p['path']) == p['sha256']} for p in before['personal_files']]
protected = [{'path': p['path'], 'unchanged': sha(ROOT / p['path']) == p['sha256']} for p in before['protected']]
artifacts = []
for platform, names in [('windows', ('MagicControl.exe', 'mctl.exe')), ('linux', ('MagicControl', 'mctl'))]:
    built = ROOT / 'build' / platform / 'bin'
    if platform == 'windows':
        built /= 'Release'
    for name in names:
        portable = ROOT / 'MagicControl' / platform / name
        artifacts.append({'platform': platform, 'name': name, 'sha256': sha(portable), 'matches_build': sha(portable) == sha(built / name)})
    assert (ROOT / 'MagicControl' / platform / 'build-version.txt').read_text(encoding='utf-8-sig').strip() == state['version']
    assert state['platforms'][platform]['status'] == 'verified'
assert state['status'] == 'complete'
assert all(p['matches'] for p in inputs)
assert all(p['unchanged'] for p in personal + protected)
assert all(p['matches_build'] for p in artifacts)
result = {'version': state['version'], 'status': state['status'], 'source_fingerprint': state['source_fingerprint'],
          'input_count': len(inputs), 'input_mismatches': [p for p in inputs if not p['matches']],
          'personal_files': personal, 'protected_ui_files': protected, 'artifacts': artifacts,
          'scope': 'Compilation plus static artifact/source inspection; no tests, GUI or hardware run.'}
(OUT / f'verification-{state["version"]}.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'version': state['version'], 'inputs_match': len(inputs), 'personal_preserved': len(personal),
                  'protected_unchanged': len(protected), 'binaries_verified': len(artifacts)}))
