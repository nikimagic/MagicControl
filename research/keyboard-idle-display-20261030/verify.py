"""Verify saved build artifacts and settings hashes; no hardware access."""
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
p=json.loads((ROOT/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert p['version']=='0.1.392' and p['status']=='complete'
artifacts=[]
for os,build,names in [('windows','build/windows/bin/Release',['MagicControl.exe','mctl.exe']),('linux','build/linux/bin',['MagicControl','mctl'])]:
    for i,name in enumerate(names):
        portable=ROOT/'MagicControl'/os/name;original=ROOT/build/name
        expected=p['platforms'][os]['sha256' if i==0 else 'cli_sha256'].lower()
        assert sha(portable)==sha(original)==expected
        artifacts.append({'path':str(portable.relative_to(ROOT)),'sha256':expected,'matches_build':True})
settings={}
for os in ['windows','linux']:
    old=json.loads((ROOT/f'build/keyboard-idle-settings-{os}-before.json').read_text(encoding='utf-8-sig'))
    changed=[{'path':r['Path'],'before':r['SHA256'],'after':sha(Path(r['Path'])) if Path(r['Path']).exists() else None}
             for r in old if not Path(r['Path']).exists() or sha(Path(r['Path']))!=r['SHA256'].lower()]
    settings[os]={'baseline_files':len(old),'changed':changed,'policy':'Preserve live user data; do not overwrite changes made by the user during the build.'}
data=(OUT/'keyboard-status.jsonl').read_bytes()
raw=data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig')
k=json.loads(raw.strip());assert k['app_version']=='0.1.392' and k['idle']['platform']==7 and k['idle']['limit_seconds']==600
result={'version':p['version'],'source_fingerprint':p['source_fingerprint'],'artifacts':artifacts,'settings':settings,
        'live_read':json.loads((OUT/'result.json').read_text(encoding='utf-8-sig')),
        'scope':'Windows/Linux GUI+CLI built; CLI keyboard/power status read-only succeeded on Wujie. No GUI startup or EC write was used for verification. No new tests were added/run (AGENTS.md). Header uses shared Scale/embedded fonts; physical 100/200% screenshot validation not performed.'}
(OUT/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'version':p['version'],'artifacts_verified':len(artifacts),'settings_changed':{key:len(v['changed']) for key,v in settings.items()},'idle':k['idle']}))
