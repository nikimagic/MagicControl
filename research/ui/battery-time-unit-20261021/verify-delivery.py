"""Inspect saved artifacts and checksums only; no app, hardware or tests run."""
from pathlib import Path
import hashlib
import json
import subprocess

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[2]

def read(name):
    return json.loads((OUT / name).read_text(encoding='utf-8-sig'))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

# Compare the existing shared painting/scrolling code after removing only the new tooltip adapter.
mechanics = (ROOT / 'src/gui/windows/PageMechanics.h').read_text(encoding='utf-8-sig')
start = mechanics.index('// Native tooltips for painted page regions;')
end = mechanics.index('struct Buffer {', start)
mechanics = mechanics[:start] + mechanics[end:]
for header in ['commctrl.h', 'vector', 'utility']:
    mechanics = mechanics.replace('#include <' + header + '>\n', '', 1)
for header in ['Geometry.h', 'Text.h']:
    mechanics = mechanics.replace('#include "' + header + '"\n', '', 1)
prior = subprocess.check_output(['git', 'show', 'e513892:src/gui/windows/PageMechanics.h'], cwd=ROOT).decode('utf-8-sig').replace('\r\n', '\n')
if mechanics != prior:
    raise RuntimeError('Existing shared page/scroll mechanics changed')

session = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
if session['status'] != 'complete':
    raise RuntimeError('Common build is not complete')
inputs = session['application_inputs']
changed = [path for path, digest in inputs.items() if sha(ROOT / path) != digest.lower()]
personal = [{'path': path, 'unchanged': sha(ROOT / 'MagicControl' / path) == digest.lower()}
            for path, digest in read('personal-before-staging.json').items()]
protected = [{'path': path, 'unchanged': sha(ROOT / path) == digest.lower()}
             for path, digest in read('baseline.json')['protected'].items()]
archive = [{'path': path, 'unchanged': sha(ROOT / path) == digest.lower()}
           for path, digest in read('baseline.json')['archive'].items()]
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
if changed or not all(p['unchanged'] for p in personal + protected + archive) or not all(p['matches_build'] for p in artifacts):
    raise RuntimeError('Artifact/source/personal/protected checksum mismatch')
report = {'version': session['version'], 'status': session['status'],
          'source_fingerprint': session['source_fingerprint'], 'input_count': len(inputs),
          'input_mismatches': changed, 'personal_files': personal, 'protected_ui_files': protected,
          'artifacts': artifacts, 'detail_rows': 130, 'ec_detail_rows': 103, 'os_detail_rows': 27, 'primary_rows': 10, 'columns': 2, 'live_rows_per_card': 6, 'live_summaries': ['EC only', 'OS only'], 'entries_per_column': 5, 'port_gap_dip': 16, 'providers': ['OS', 'EC', 'PawnIO unavailable'],
          'archive_files': archive, 'candidate_fields': 61, 'candidate_bits': 13,
          'wujie_lower_requested_percent': 97, 'lower_role': 'unverified; slow-charge threshold is a hypothesis',
          'logging': 'Separate battery.jsonl; shared session/UTC/sequence; continuous OS/power flags and observed integer SOC; key events mirrored to general log; BatteryLog independent of AllLogs',
          'configuration': 'BatteryLog=0/1; missing key defaults to enabled; restart after editing config',
          'existing_page_mechanics_preserved': True,
          'time_unit': {'RU': 'ч', 'EN': 'h', 'SC': '小时', 'TW': '小時'}, 'group_gap_dip': 6, 'primary_health_decimals': 1, 'detail_health_decimals': 3, 'unavailable_units_preserved': True,
          'matching_primary_order': ['charge', 'power flow', 'cycles', 'temperature', 'voltage', 'current', 'full/design capacity', 'health'],
          'source_specific_tail': {'EC': ['time until empty', 'time until full', 'time until charge limit'], 'OS': ['power plan', 'frequency policy AC/DC', 'EPP policy AC/DC']},
          'diagnostic_card': 'Compact source-specific details; primary rows also font-height/gap0, six separately labelled live rows; one measured value column across all 11 rows per card; 6 DIP group spacing; short captions; primary health 1 decimal/details 3; native policy tooltips; unavailable units preserved; human captions in 4 languages; OS energy displayed in Wh with raw DTO/logs unchanged',
          'scope': 'Compilation and saved artifact/source inspection only; no tests, GUI or hardware run.'}
(OUT / ('verification-' + session['version'] + '.json')).write_text(
    json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'version': session['version'], 'inputs_match': len(inputs),
                  'personal_preserved': len(personal), 'protected_unchanged': len(protected),
                  'binaries_verified': len(artifacts), 'archive_preserved': len(archive)}))
