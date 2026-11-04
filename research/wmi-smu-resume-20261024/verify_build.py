"""Build/source/portable metadata checks; does not run the application or devices."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = '1e5801c'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().lower()


def previous(name):
    return subprocess.check_output(['git', 'show', f'{BASE}:{name}'], cwd=ROOT).decode('utf-8')


state = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
assert state['status'] == 'complete'
version = state['version']
files = []
for platform, names in (('windows', ('MagicControl.exe', 'mctl.exe')),
                        ('linux', ('MagicControl', 'mctl'))):
    assert state['platforms'][platform]['status'] == 'verified'
    portable = ROOT / 'MagicControl' / platform
    built = ROOT / 'build' / platform / 'bin'
    if platform == 'windows':
        built /= 'Release'
    assert (portable / 'build-version.txt').read_text().strip() == version
    assert (ROOT / 'build' / platform / 'generated/MagicControl/Version.h.version').read_text().strip() == version
    for name in names:
        digest = sha(portable / name)
        assert digest == sha(built / name)
        files.append({'platform': platform, 'file': name, 'version': version, 'sha256': digest})

before = json.loads((ROOT / 'build/wmi-smu-personal-before.json').read_text())
changes = [name for name, digest in before.items()
           if not (ROOT / name).is_file() or sha(ROOT / name) != digest]
assert not changes, changes
assert all(sha(ROOT / name) == digest.lower() for name, digest in state['application_inputs'].items())

reader = 'src/platform/windows/pawnio/ApuPowerWindows.cpp'
old, new = previous(reader), (ROOT / reader).read_text(encoding='utf-8')
def setters(text):
    return text.split('    void mp1Limit(', 1)[1].split('    ApuPowerDiagnostic read(', 1)[0]


assert setters(old) == setters(new)
assert old.split('            std::array<std::uint64_t, 4> raw{};', 1)[1] == new.split('            std::array<std::uint64_t, 4> raw{};', 1)[1]
event_source = 'src/platform/windows/OemEventMonitor.cpp'
assert previous(event_source).split('    while (!stop_', 1)[1] == (ROOT / event_source).read_text().split('    while (!stop_', 1)[1]
protected = []
for tree in ('src/devices', 'src/protocols', 'src/gui', 'src/core/include'):
    for path in (ROOT / tree).rglob('*'):
        if not path.is_file():
            continue
        name = path.relative_to(ROOT).as_posix()
        actual_bytes = path.read_bytes()
        old_bytes = subprocess.check_output(['git', 'show', f'{BASE}:{name}'], cwd=ROOT)
        try:
            unchanged = actual_bytes.decode('utf-8').replace('\r\n', '\n') == old_bytes.decode('utf-8').replace('\r\n', '\n')
        except UnicodeDecodeError:
            unchanged = actual_bytes == old_bytes
        assert unchanged, name
        protected.append(name)

source_files = [reader, event_source,
    'research/devices/hlyl-wxx9/sources/PawnIO.Modules/RyzenSMU.p',
    'research/devices/hlyl-wxx9/sources/RyzenAdj/lib/api.c']
evidence = json.loads(Path(__file__).with_name('EVIDENCE.json').read_text())
report = {'version': version, 'base_revision': BASE, 'common_build': state['status'],
          'scope': 'Source/build/package metadata only; no hardware or product test execution',
          'files': files, 'personal_settings_preserved': len(before),
          'application_input_hashes_verified': len(state['application_inputs']),
          'source_fingerprint': state['source_fingerprint'],
          'source_sha256': {name: sha(ROOT / name) for name in source_files},
          'protected_source_files_unchanged': len(protected),
          'honor_limit_setters_and_rollback_unchanged': True,
          'pm_decoder_stale_reset_and_30s_backoff_unchanged': True,
          'event_delivery_loop_unchanged': True,
          'source_pm_error_count': evidence['pm_error_count'],
          'source_pm_recovery_delay_seconds': evidence['pm_error_to_recovery_seconds'],
          'application_launched': False, 'ec_access': False, 'new_tests_run': False,
          'resume_hardware_recovery_verified': False, 'github_archives_updated': False}
out = Path(__file__).with_name(f'VERIFICATION-{version}.json')
out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))
