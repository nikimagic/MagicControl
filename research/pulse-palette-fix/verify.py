"""Verify saved build receipts and portable files without launching the application."""
from pathlib import Path
import hashlib, json
root = Path(__file__).resolve().parents[2]
receipt = json.loads((root / 'build/product-build.json').read_text(encoding='utf-8-sig'))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
mismatches = [p for p, expected in receipt['application_inputs'].items() if sha(root / p) != expected.lower()]
assert not mismatches, mismatches
assert receipt['status'] == 'complete' and receipt['version'] == '0.1.188'
result = {'version': receipt['version'], 'source_fingerprint': receipt['source_fingerprint'],
          'application_inputs_checked': len(receipt['application_inputs']), 'source_mismatches': mismatches,
          'hardware_launched': False, 'tests_run': False, 'platforms': {}}
for platform, executable in [('windows', 'MagicControl.exe'), ('linux', 'MagicControl')]:
    portable = root / 'MagicControl' / platform
    path = portable / executable
    actual = sha(path)
    version = (portable / 'build-version.txt').read_text(encoding='utf-8-sig').strip()
    assert actual == receipt['platforms'][platform]['sha256'].lower()
    assert version == receipt['version']
    result['platforms'][platform] = {'path': str(path), 'sha256': actual, 'version': version,
                                     'matches_verified_build': True}
(Path(__file__).with_name('verification.json')).write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, indent=2))
