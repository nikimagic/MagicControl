"""Read saved build metadata and hashes only. Does not launch the application/tests."""
from pathlib import Path
import hashlib, json

root = Path(__file__).resolve().parents[2]
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest().upper()
session = json.loads((root/'build/product-build.json').read_text(encoding='utf-8-sig'))
if session['status'] != 'complete': raise SystemExit('Common product build is not complete')
version = session['version']
files = []
for platform, names in (('windows', ('MagicControl.exe', 'mctl.exe')), ('linux', ('MagicControl', 'mctl'))):
    if session['platforms'][platform]['status'] != 'verified': raise SystemExit('Unverified '+platform)
    portable = root/'MagicControl'/platform
    built = root/'build'/platform/'bin'
    if platform == 'windows': built /= 'Release'
    sidecar = (portable/'build-version.txt').read_text().strip()
    generated = (root/'build'/platform/'generated/MagicControl/Version.h.version').read_text().strip()
    if sidecar != version or generated != version: raise SystemExit('Version mismatch '+platform)
    for name in names:
        digest = sha(portable/name)
        if digest != sha(built/name): raise SystemExit('Binary mismatch '+platform+'/'+name)
        files.append({'platform':platform, 'name':name, 'version':version,
                      'sha256':digest, 'matches_build':True})

before = json.loads((root/'build/log-version-personal-before.json').read_text(encoding='utf-8'))
changed = [name for name, digest in before.items()
           if not (root/name).is_file() or sha(root/name).lower() != digest.lower()]
if changed: raise SystemExit('Preserved files changed: '+repr(changed))
report = {'version':version, 'common_build':'complete', 'files':files,
          'source_fingerprint':session['source_fingerprint'],
          'personal_files_preserved':len(before), 'personal_files_changed':changed,
          'tests_run':False, 'hardware_tests_run':False, 'application_launched':False,
          'scope':'Shared logger app_version in session_start and diagnostic snapshot_scope on Windows/Linux. Static review and build/hash checks only.',
          'github_archives_updated':False}
out = Path(__file__).resolve().parent/f'VERIFICATION-{version}.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))
