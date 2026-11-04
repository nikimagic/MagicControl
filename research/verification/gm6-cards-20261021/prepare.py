"""Capture build preservation baseline; read files only, never run app/backend."""
from pathlib import Path
import hashlib,json,subprocess,shutil
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=json.loads((OUT.parent/'power-card-first-20261021/baseline.json').read_text(encoding='utf-8'))
changed=set(subprocess.check_output(['git','diff','--name-only'],cwd=ROOT,text=True).splitlines())
base={k:v for k,v in old.items()if k in ('archive','runtime_sources','metadata')}
base['archive']={p:sha(ROOT/p)for p in old['archive']}
base['protected']={p:h for p,h in old['protected'].items()if p not in changed and not p.startswith('MagicControl/')}
base['personal']={str(p.relative_to(ROOT/'MagicControl')).replace('\\','/'):sha(p)
 for folder in ('windows','linux')for p in (ROOT/'MagicControl'/folder).rglob('*')
 if p.is_file()and(p.suffix in ('.ini','.csv','.jsonl','.log')or p.name in ('bios-inventory.json','battery-enforcement-platform.txt'))}
for kind in ('archive','runtime_sources','metadata','protected'):
 for p,h in base[kind].items():assert sha(ROOT/p)==h,p
(OUT/'baseline.json').write_text(json.dumps(base,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(OUT/'personal-before-staging.json').write_text(json.dumps(base['personal'],indent=2)+'\n',encoding='utf-8')
for platform in ('windows','linux'):shutil.copyfile(ROOT/f'build/offline/{platform}/results.xml',OUT/f'tests-{platform}.xml')
test_inputs={str(p.relative_to(ROOT)).replace('\\','/'):sha(p)for p in (ROOT/'tests/offline').rglob('*')if p.is_file()}
(OUT/'test-inputs.json').write_text(json.dumps(test_inputs,indent=2)+'\n',encoding='utf-8')
print({k:len(v)for k,v in base.items()})
