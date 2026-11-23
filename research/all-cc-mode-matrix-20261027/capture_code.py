"""Extract numbered original class ranges; no execution of analysed assemblies."""
from pathlib import Path
import re,json,hashlib,sys
root=Path(__file__).resolve().parents[2];here=Path(__file__).resolve().parent
sys.path.insert(0,str(root/'research/ec-platform-catalogue-20261023'))
from relocated_evidence import NESTED_OEM,read_verified,new_run_dir
out=new_run_dir('cc-mode-capture-code')
w=root/'research/ec-platform-catalogue-20261023/managed-provenance/GCUService-readable-strings.cs'
text=w.read_text(encoding='utf-8-sig');lines=text.splitlines();manifest=[]
for name in ('MyEcCtrl','MyFanCtrl','PowerModeEvent','MyFanManager_RamFan1p5','FanTable_Manager1p5','MyFanTableCtrl','BatteryProtection2'):
    pat=re.compile(r'^(\s*)(?:(?:public|internal|private|protected|sealed|static|abstract|unsafe)\s+)*class\s+'+re.escape(name)+r'\b')
    found=next((i for i,line in enumerate(lines) if pat.match(line)),None)
    assert found is not None,name
    indent=pat.match(lines[found]).group(1)
    next_type=re.compile(r'^'+re.escape(indent)+r'(?:(?:public|internal|private|protected|sealed|static|abstract|unsafe)\s+)*(?:class|enum|interface|struct)\s+\S')
    end=next((i for i in range(found+1,len(lines)) if next_type.match(lines[i])),len(lines))
    p=out/f'WUJIE557-{name}.txt'
    p.write_text(str(w)+'\n'+'\n'.join(f'{i+1:06d} {lines[i]}' for i in range(found,end))+'\n',encoding='utf-8')
    manifest.append(dict(id='WUJIE557-'+name,path=str(w),source_sha256=hashlib.sha256(w.read_bytes()).hexdigest(),
                         first_line=found+1,last_line=end,numbered_copy=p.name))
recovery=json.loads((NESTED_OEM/'7578-recovered-data/analysis-assembly-recovery.json').read_text(encoding='utf-8'))
analysis_path=NESTED_OEM/'7578-recovered-data/GCUService-recovered-analysis.bin'
analysis,_=read_verified(analysis_path,recovery['analysis_sha256'])
for p in (here/'CC556-7578-RamFan.cs',here/'CC556-Power.cs',here/'CC556-FanTable.cs',here/'CC556-FanCtrl.cs'):
    manifest.append(dict(id=p.stem,path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
                         input=str(analysis_path),analysed_image_sha256=hashlib.sha256(analysis).hexdigest()))
original=NESTED_OEM/'7578cb273f0f99fe414a681b49bebc952f25ecba71885dbad608a19f8b71455f-GCUService.exe.bin'
read_verified(original,'7578cb273f0f99fe414a681b49bebc952f25ecba71885dbad608a19f8b71455f')
pin=root/'research/ec-platform-catalogue-20261023/managed-provenance/input-comparison.json'
assert all(x['identical'] for x in json.loads(pin.read_text(encoding='utf-8-sig')))
manifest.append(dict(id='WUJIE557-byte-identity',path=str(pin),sha256=hashlib.sha256(pin.read_bytes()).hexdigest()))
(out/'CODE-EVIDENCE.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Saved7 exact WUJIE classes,4 freshly decompiled CC5.56 classes, and pinned source identity')
