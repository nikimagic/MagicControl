"""Inspect staged build artifacts and saved hashes; never starts an application."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
state = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
before = json.loads((OUT / 'before.json').read_text(encoding='utf-8-sig'))
version = state['version']
inputs = [{'path': p, 'matches': sha(ROOT / p) == h.lower()}
          for p, h in state['application_inputs'].items()]
settings = [{'path': x['path'], 'unchanged': sha(x['path']) == x['sha256']} for x in before['settings']]
protected = [{'path': x['path'], 'unchanged': sha(ROOT / x['path']) == x['sha256']} for x in before['protected']]
layout_before = subprocess.check_output(['git','show','66e7834:src/gui/common/Layout.h'], cwd=ROOT, encoding='utf-8')
layout_after = (ROOT / 'src/gui/common/Layout.h').read_text(encoding='utf-8')
def layout_dimensions(text):
    # The owner requested a navigation caption change in this dimensions header.
    # Exclude exactly its title table/alias, include and size assertion, nothing else.
    return [line for line in text.splitlines() if not (
        line.strip() == '#include "TabLabels.h"' or
        line.startswith('inline constexpr const wchar_t* kTabTitles[') or
        line.startswith('inline constexpr auto& kTabTitles =') or
        line.startswith('static_assert(kTabTitles.size()==kTabCount)'))]
dimensions_unchanged = layout_dimensions(layout_before) == layout_dimensions(layout_after)
artifacts = []
for os, gui, cli in [('windows', 'MagicControl.exe', 'mctl.exe'), ('linux', 'MagicControl', 'mctl')]:
    built = ROOT / 'build' / os / 'bin'
    if os == 'windows':
        built /= 'Release'
    portable = ROOT / 'MagicControl' / os
    for name in (gui, cli):
        artifacts.append({'platform': os, 'name': name, 'sha256': sha(portable / name),
                          'matches_build': sha(portable / name) == sha(built / name)})
    for name in ('NotoSansSC.ttf', 'NotoSansTC.ttf', 'OFL-NotoSansSC.txt', 'OFL-NotoSansTC.txt', 'NOTICE.txt'):
        artifacts.append({'platform': os, 'name': 'fonts/' + name, 'sha256': sha(portable / 'fonts' / name),
                          'matches_source': sha(portable / 'fonts' / name) == sha(ROOT / 'src/gui/assets/fonts' / name)})
    assert (portable / 'build-version.txt').read_text(encoding='utf-8-sig').strip() == version
    assert state['platforms'][os]['status'] == 'verified'
assert state['status'] == 'complete'
assert all(x['matches'] for x in inputs)
assert all(x['unchanged'] for x in settings)
assert dimensions_unchanged
assert all(x['unchanged'] for x in protected if x['path'] != 'src/gui/common/Layout.h')
assert all(x.get('matches_build', x.get('matches_source', False)) for x in artifacts)
result = {'version': version, 'state': state['status'], 'source_fingerprint': state['source_fingerprint'],
          'input_count': len(inputs), 'input_mismatches': [x for x in inputs if not x['matches']],
          'settings': settings, 'protected_files': protected, 'artifacts': artifacts,
          'layout_unchanged_except_fixed_navigation_caption_source': dimensions_unchanged,
          'scope': 'Artifact/source/settings inspection only; no tests, GUI or EC run.',
          'visual_review': 'Not performed; reference geometry/line boxes retained.'}
(OUT / f'verification-{version}.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'version': version, 'inputs': len(inputs), 'inputs_match': True,
                  'settings_preserved': len(settings), 'artifacts_verified': len(artifacts),
                  'protected_changes': [x['path'] for x in protected if not x['unchanged']]}, ensure_ascii=False))
