"""Keep observed identities separate from OEM defaults and registry reports."""
from pathlib import Path
import json,hashlib
root=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent
source=root/'research/cc-model-mapping-20261026/mapping-evidence.json'
mapping=json.loads(source.read_text(encoding='utf-8-sig'))
reports=json.loads((root/'research/cc-model-mapping-20261026/observations.json').read_text(encoding='utf-8-sig'))['reports']
rows=['НАБЛЮДЕНИЯ, НЕ ЗАВОДСКИЕ КОНСТАНТЫ. Часть снята после наших записей.',
      'Плата | Project740 | BIOS family | Platform7C3 | ROM770 | источник','']
for board,v in mapping['observed_boards'].items():
    bios=sorted({q['name'] for q in v['BIOS_codec']})
    rows.append(f"{board} | {v['project_740']} | {','.join(bios)} | {v['platform_7C3']} | "+
                ','.join(f'{n:02X}' for n in v['ROM_770'])+' | сохранённые RAM с validity, индекс26.10')
rows.extend(['','ДляWUJIE дополнительный свежий файл portable platform_id/rom_id:'])
startup=[]
for name in ('platform_id','rom_id','project_id'):
    p=root/'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG'/name
    last=json.loads(p.read_text(encoding='utf-8-sig').splitlines()[-1])
    startup.append(dict(path=str(p),record=last))
    rows.append(name+' | '+last.get('local_time','')+' | '+json.dumps(last.get('observed'),ensure_ascii=False))
rows.extend(['','ДРУГИЕ МОДЕЛИ: ProjectID из OEM РЕЕСТРА, НЕ direct740. Platform/ROM неизвестны.',
             'Плата | CC version(s) | registry ProjectID | CustomizeTarget'])
by={}
for q in reports:
    b=q['board'];v=by.setdefault(b,dict(versions=set(),projects=set(),targets=set()))
    for key,f in [('versions','CC_version'),('projects','registry_ProjectID'),('targets','registry_CustomizeTarget')]:
        if q.get(f) is not None:v[key].add(q[f])
for board,v in by.items():
    rows.append(f"{board} | {sorted(v['versions'])} | {sorted(v['projects'])} | {sorted(v['targets'])}")
(out/'IDENTITIES.txt').write_text('\n'.join(rows)+'\n',encoding='utf-8')
(out/'IDENTITIES.json').write_text(json.dumps(dict(
    mapping_source=str(source),mapping_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    observed_boards=mapping['observed_boards'],fresh_wujie_history=startup,
    registry_reports=reports,factory_identity_claimed=False),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Identity matrix saved:',len(mapping['observed_boards']),'RAM boards,',len(by),'registry boards')
