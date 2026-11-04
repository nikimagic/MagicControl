"""File-only, pinned local firmware fixtures. Never runs a vendor/product binary."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

p = argparse.ArgumentParser()
p.add_argument('--reader', required=True, type=Path, help='The isolated pure-validator test executable, never AFU/FPT/NVFlash')
p.add_argument('--repo', required=True, type=Path)
p.add_argument('--scratch', required=True, type=Path)
a = p.parse_args()
manifest = json.loads(Path(__file__).with_name('real-files.json').read_text(encoding='utf-8'))
missing = [f['id'] for f in manifest['fixtures'] if not (a.repo / f['path']).is_file()]
if missing:
    print('SKIP private fixtures absent: ' + ', '.join(missing))
    sys.exit(77)
a.scratch.mkdir(parents=True, exist_ok=True)
results = []
for f in manifest['fixtures']:
    source = a.repo / f['path']
    if 'member' in f:
        with zipfile.ZipFile(source) as z:
            entry = z.getinfo(f['member'])
            assert entry.file_size == f['bytes'], f['id'] + ': ZIP length'
            data = z.read(entry)
    else:
        assert source.stat().st_size == f['bytes'], f['id'] + ': source length'
        data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert len(data) == f['bytes'] and digest == f['sha256'], f['id'] + ': fixture hash mismatch'
    # Generated safe name; never extract archive paths or change original logs.
    image = a.scratch / (f['id'] + '.bin')
    image.write_bytes(data)
    completed = subprocess.run([str(a.reader.resolve()), 'inspect', f['route'], str(image.resolve()), str(f['smbios_bytes'])],
                               capture_output=True, text=True, encoding='utf-8', timeout=60)
    assert completed.returncode == (0 if f['accepted'] else 4), f['id'] + ': unexpected result: ' + completed.stderr
    result = {'id': f['id'], 'bytes': len(data), 'sha256': digest, 'expected_accepted': f['accepted']}
    if f['accepted']:
        shape = json.loads(completed.stdout)
        assert shape['kind'] == f['kind']
        if f['route'] == 'intel':
            assert shape['descriptor_bytes'] == f['descriptor_bytes']
            assert shape['firmware_volumes'] >= 2 and shape['all_regions_readable']
        else:
            assert shape['descriptor_bytes'] == 0 and shape['components'] == 0
        result['shape'] = shape
    else:
        assert f['reason'] in completed.stderr, f['id'] + ': wrong refusal reason'
        result['refusal'] = completed.stderr.strip()
    # The validator is also required to leave the copy byte-for-byte untouched.
    assert hashlib.sha256(image.read_bytes()).hexdigest() == digest
    results.append(result)
    print('PASS pinned file: ' + f['id'])
report = {'hardware_executed': False, 'vendor_or_product_executed': False, 'fixtures': results,
          'unavailable_real_fixtures': manifest['unavailable_real_fixtures']}
(a.scratch / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('PASS 5 pinned local fixtures; 4 accepted structural files and 1 refused region; originals unchanged.')
