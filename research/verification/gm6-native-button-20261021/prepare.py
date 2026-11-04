"""Capture build preservation baseline; read files only, never run app/backend."""
from pathlib import Path
import hashlib,json,subprocess,shutil
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=json.loads((OUT.parent/'gm6-cards-20261021/baseline.json').read_text(encoding='utf-8'))
changed=set(subprocess.check_output(['git','diff','--name-only'],cwd=ROOT,text=True).splitlines())
base={k:v for k,v in old.items()if k in ('archive','runtime_sources','metadata')}
base['archive']={p:sha(ROOT/p)for p in old['archive']}
base['protected']={p:h for p,h in old['protected'].items()if p not in changed and not p.startswith('MagicControl/')}
base['personal']={str(p.relative_to(ROOT/'MagicControl')).replace('\\','/'):sha(p)
 for folder in ('windows','linux')for p in (ROOT/'MagicControl'/folder).rglob('*')
 if p.is_file()and(p.suffix in ('.ini','.csv','.jsonl','.log')or p.name in ('bios-inventory.json','battery-enforcement-platform.txt'))}
for kind in ('archive','runtime_sources','metadata','protected'):
 base[kind]={p:sha(ROOT/p) for p in base[kind]}
(OUT/'baseline.json').write_text(json.dumps(base,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(OUT/'personal-before-staging.json').write_text(json.dumps(base['personal'],indent=2)+'\n',encoding='utf-8')
# Preserve the battery decoder/reader body inside the edited GM6 controller.
path='src/devices/mechrevo/gm6ir0c/ControlWindows.cpp'
reference=subprocess.check_output(['git','show','31620e8:'+path],cwd=ROOT).decode('utf-8-sig')
def battery_body(s):return s.split('EcBatteryReading readGm6EcBatteryOnly()',1)[1].split('Gm6Controller::Gm6Controller()',1)[0]
assert battery_body(reference)==battery_body((ROOT/path).read_text(encoding='utf-8-sig'))
(OUT/'battery-body-sha.json').write_text(json.dumps({'sha256':hashlib.sha256(battery_body(reference).encode()).hexdigest()}),encoding='utf-8')
print({k:len(v)for k,v in base.items()})
