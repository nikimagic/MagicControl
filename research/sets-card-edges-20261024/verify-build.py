"""Inspect built artifacts/settings only. Does not execute the application."""
from pathlib import Path
import datetime, hashlib, json, re, subprocess

root = Path(__file__).resolve().parents[2]
folder = Path(__file__).resolve().parent
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
session = json.loads((root/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert session['version'] == '0.1.178' and session['status'] == 'complete'
spec = json.loads((root/'cmake/ApplicationInputs.json').read_text())
paths = {root/name for name in spec['files']}
for tree in spec['trees']:
    paths.update(path for path in (root/tree).rglob('*') if path.is_file())
inputs = {path.relative_to(root).as_posix(): digest(path) for path in paths}
assert inputs == session['application_inputs']
rows = []
for platform, executables in [('windows', ['MagicControl.exe', 'mctl.exe']), ('linux', ['MagicControl', 'mctl'])]:
    generated = root/'build'/platform/'generated/MagicControl/Version.h.version'
    portable = root/'MagicControl'/platform
    assert generated.read_text().strip() == (portable/'build-version.txt').read_text().strip() == session['version']
    assert session['platforms'][platform]['status'] == 'verified'
    for index, name in enumerate(executables):
        built = root/'build'/platform/'bin'/('Release' if platform == 'windows' else '')/name
        actual = digest(portable/name)
        assert actual == digest(built)
        if index == 0:
            assert actual == session['platforms'][platform]['sha256'].lower()
        rows.append(dict(platform=platform, file=name, sha256=actual, portable_matches_build=True))
baseline = json.loads((folder/'personal-before.json').read_text())
changed = [name for name, sha in baseline.items() if not (root/name).is_file() or digest(root/name) != sha]
assert not changed, changed
tests=[]
for platform in ['windows','linux']:
    log=(folder/f'test-{platform}.log').read_text(encoding='utf-8-sig')
    assert 'SETs geometry: 2240 cases, 45920 checks;' in log
    tests.append(dict(platform=platform,target='settings_geometry_offline_contract',cases=2240,checks=45920,passed=True))
unchanged=[]
for name in ['src/core/src/ScrollModel.cpp','src/gui/windows/FansPage.cpp','src/gui/windows/KeysPage.cpp',
             'src/gui/windows/BiosPage.cpp','src/gui/windows/SettingsPage.cpp','src/gui/linux/SettingsPage.cpp',
             'src/gui/linux/Ui.cpp','src/gui/windows/MainWindow.cpp']:
    original=subprocess.check_output(['git','show','HEAD:'+name],cwd=root)
    assert (root/name).read_bytes().replace(b'\r\n',b'\n')==original.replace(b'\r\n',b'\n'),name
    unchanged.append(name)
for name in ['src/gui/windows/PageMechanics.h','src/gui/common/Layout.h']:
    original=subprocess.check_output(['git','show','HEAD:'+name],cwd=root)
    code=lambda data:re.sub(rb'\s+',b'',re.sub(rb'//[^\r\n]*',b'',data))
    assert code((root/name).read_bytes())==code(original),name
report = dict(version=session['version'], captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              common_build_complete=True, status='verified', binaries=rows, application_inputs_match=len(inputs),
              personal_settings_history_preserved=len(baseline), personal_files_changed=changed,
              application_executed=False, hardware_io_executed_by_assistant=False, tests_run=tests,
              gui_runtime_verified=False, unchanged_scroll_and_page_sources=unchanged,
              scroll_mechanics_and_dimensions_comments_only=True, github_release_changed=False)
(folder/'VERIFICATION-0.1.178.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps(report, indent=2))
