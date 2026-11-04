"""Build/settings/source verification only; does not touch hardware."""
from pathlib import Path
import hashlib
import json
import re
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
p=json.loads((ROOT/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert p['version']=='0.1.393' and p['status']=='complete'
artifacts=[]
for os,build,names in [('windows','build/windows/bin/Release',['MagicControl.exe','mctl.exe']),('linux','build/linux/bin',['MagicControl','mctl'])]:
    for i,name in enumerate(names):
        a=ROOT/build/name;b=ROOT/'MagicControl'/os/name
        expected=p['platforms'][os]['sha256' if i==0 else 'cli_sha256'].lower()
        assert sha(a)==sha(b)==expected
        artifacts.append({'path':str(b.relative_to(ROOT)),'sha256':expected,'matches_build':True})
settings={}
for os in ['windows','linux']:
    rows=json.loads((ROOT/f'build/keys-native-settings-{os}-before.json').read_text(encoding='utf-8-sig'))
    changed=[{'path':row['Path'],'before':row['SHA256'],'after':sha(Path(row['Path'])) if Path(row['Path']).exists() else None}
             for row in rows if not Path(row['Path']).exists() or sha(Path(row['Path']))!=row['SHA256'].lower()]
    settings[os]={'baseline_files':len(rows),'changed':changed,'policy':'Keep live files, never overwrite user changes with the baseline.'}
texts={str(f.relative_to(ROOT)):f.read_text(encoding='utf-8-sig') for f in (ROOT/'src').rglob('*') if f.suffix in ['.cpp','.h']}
old_tokens=['CopilotKeyBlocker','copilotBlocked','synchronizeFirmware','createCopilotFirmwareControl','VK_F23','F23_events']
legacy={token:[name for name,text in texts.items() if token in text] for token in old_tokens}
assert not any(legacy.values())
firmware=(ROOT/'src/platform/windows/CopilotFirmwareWindows.cpp').read_text(encoding='utf-8-sig')
hardware=(ROOT/'src/platform/windows/KeysHardwareWindows.cpp').read_text(encoding='utf-8-sig')
definitions={name:len(re.findall(r'\bvoid\s+'+name+r'\([^;]*?\)\s*\{',firmware if name=='applyCopilotFirmware' else hardware))
             for name in ['applyCopilotFirmware','applyKeyHardware','setCopilotResource']}
assert all(value==1 for value in definitions.values())
assert 'Copilot' not in (ROOT/'src/gui/windows/MainWindow.cpp').read_text(encoding='utf-8-sig')
commands=json.loads((OUT/'result.json').read_text(encoding='utf-8-sig'))
assert commands['elevated'] and commands['version']=='0.1.393'
assert len(commands['read_only_commands'])==10 and all(r['exit_code']==0 for r in commands['read_only_commands'])
observed={}
for row in commands['read_only_commands']:
    b=(OUT/(row['key']+'.jsonl')).read_bytes()
    s=json.loads(b.decode('utf-16' if b.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig'))
    assert s['app_version']=='0.1.393'
    observed[row['key']]=s
keys={line.split('=',1)[0]:line.split('=',1)[1] for line in (ROOT/'MagicControl/windows/settings/keys-settings.ini').read_text(encoding='utf-8-sig').splitlines() if '=' in line}
result={'version':p['version'],'source_fingerprint':p['source_fingerprint'],'artifacts':artifacts,
        'settings':settings,'legacy_tokens_in_product':legacy,'command_entry_definitions':definitions,
        'read_only_statuses':observed,'osd_disabled_saved':keys.get('OsdDisabled'),
        'native_protocol_files_unchanged':['src/protocols/uniwill/QuickSwitch.cpp','src/devices/mechrevo/gm6ir0c/ControlWindows.cpp'],
        'scope':'Both OS GUI+CLI built;10 Windows getters read on Wujie. No physical switching or sleep test in this verification. Existing test expectations updated, tests not run. No GUI launch, hook or EC writes from the read-only collector. Live files retained.'}
(OUT/'verification.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({'version':p['version'],'artifacts':len(artifacts),'read_statuses':len(observed),'definitions':definitions,
                  'settings_changed':{key:len(value['changed']) for key,value in settings.items()}}))
