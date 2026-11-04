"""Verify build, saved diagnostic data and source boundaries; no hardware IO."""
from pathlib import Path
import hashlib
import json
import re
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
product=json.loads((ROOT/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert product['version']=='0.1.394' and product['status']=='complete'
artifacts=[]
for os,folder,names in [('windows','build/windows/bin/Release',['MagicControl.exe','mctl.exe']),('linux','build/linux/bin',['MagicControl','mctl'])]:
    for i,name in enumerate(names):
        build=ROOT/folder/name;live=ROOT/'MagicControl'/os/name
        expected=product['platforms'][os]['sha256' if i==0 else 'cli_sha256'].lower()
        assert sha(build)==sha(live)==expected
        artifacts.append({'path':str(live.relative_to(ROOT)),'sha256':expected,'matches_build':True})
settings={}
for os in ['windows','linux']:
    old=json.loads((ROOT/f'build/microphone-led-settings-{os}-before.json').read_text(encoding='utf-8-sig'))
    changed=[{'path':r['Path'],'before':r['SHA256'],'after':sha(Path(r['Path'])) if Path(r['Path']).exists() else None}
             for r in old if not Path(r['Path']).exists() or sha(Path(r['Path']))!=r['SHA256'].lower()]
    settings[os]={'baseline_files':len(old),'changed':changed,'policy':'Keep live files; owner tested the buttons. Never restore baseline over user choices.'}
probe=json.loads((OUT/'result.json').read_text(encoding='utf-8-sig'))
assert probe['elevated'] and probe['version']=='0.1.394' and probe['native_exit']==probe['no_ec_exit']==0
states={}
for name in ['status','status-no-ec']:
    p=OUT/(name+'.jsonl');b=p.read_bytes()
    state=json.loads(b.decode('utf-16' if b.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig'))
    assert state['app_version']=='0.1.394' and state['available'] and state['enabled'] is not None
    p.write_bytes((json.dumps(state,ensure_ascii=False,separators=(',',':'))+'\n').encode('utf-8'))
    states[name]=state
assert 'flag=unavailable' in states['status-no-ec']['status']
native=(ROOT/'src/platform/windows/MicrophoneKeysWindows.cpp').read_text(encoding='utf-8-sig')
protocol=(ROOT/'src/protocols/uniwill/QuickSwitch.cpp').read_text(encoding='utf-8-sig')
hardware=(ROOT/'src/platform/windows/KeysHardwareWindows.cpp').read_text(encoding='utf-8-sig')
counts={name:len(re.findall(r'\bvoid\s+'+name+r'\([^;]*?\)\s*\{',text)) for name,text in [
    ('applyMicrophoneKey',native),('setMicrophoneResource',hardware),('setMicrophoneMuteLed',protocol)]}
assert all(v==1 for v in counts.values())
assert 'if(!observeOnly)setDefaultMicrophoneEnabled(enabled);' in native
assert 'updateBitsVerified(0x7a6,4,muted?4:0)' in protocol
assert not any(t in native for t in ['std::thread','std::jthread','SetTimer','sleep_for'])
windows=(ROOT/'src/platform/windows/WindowsDevices.cpp').read_text(encoding='utf-8-sig')
assert 'Microphone commands must use the KEYs resource handler' in windows
assert 'setDefaultMicrophoneEnabled(' not in windows
physical=json.loads((OUT/'physical-validation.json').read_text(encoding='utf-8'))
writes=[r for r in physical['events'] if r.get('event')=='ec_write_requested']
assert any(r['address']==0x7a6 and r['value']==4 for r in writes)
assert any(r['address']==0x7a6 and r['value']==0 for r in writes)
record={'version':product['version'],'source_fingerprint':product['source_fingerprint'],'artifacts':artifacts,
        'settings':settings,'read_only_states':states,'definition_counts':counts,
        'physical':{'microphone_switching':'Owner confirmed working','EC_indicator_flag':'Off/On04/00 readback confirmed','physical_LED':'Owner did not see a LED','sleep':'Not physically tested'},
        'scope':'Windows/Linux GUI+CLI built. Read-only CLI succeeds with and without EC. No automated test suite or unrequested hardware toggles. Native indicator mapping only on exact awake Wujie; others retain their existing microphone backend.'}
(OUT/'verification.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'version':product['version'],'artifacts':len(artifacts),'definitions':counts,
                  'settings_changed':{k:len(v['changed']) for k,v in settings.items()},'physical_mic':'working','physical_LED':'not visible'}))
