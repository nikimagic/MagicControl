from pathlib import Path,PurePosixPath
import zipfile,hashlib,json,stat,uuid
src=Path('Users_logs/logs_GM6IR0C (4).zip');root=(Path('out/gm6ir0c-logs4-20261021')/('extract-'+uuid.uuid4().hex)).resolve();root.mkdir(parents=True,exist_ok=False);records=[]
with zipfile.ZipFile(src)as z:
 if sum(x.file_size for x in z.infolist())>500_000_000:raise SystemExit('Archive exceeds bounded inspection size')
 for x in z.infolist():
  p=PurePosixPath(x.filename.replace('\\','/'))
  if p.is_absolute()or'..'in p.parts or any(':'in q for q in p.parts)or stat.S_ISLNK(x.external_attr>>16):raise SystemExit('Unsafe archive path')
  dest=root.joinpath(*p.parts).resolve()
  if not dest.is_relative_to(root):raise SystemExit('Path escape')
  if x.is_dir():dest.mkdir(parents=True,exist_ok=True);continue
  dest.parent.mkdir(parents=True,exist_ok=True);b=z.read(x)
  with dest.open('xb')as f:f.write(b)
  records.append({'path':x.filename,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
out=root;manifest={'source_zip':str(src),'source_bytes':src.stat().st_size,'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'extracted_to':str(root),'scope':'Files only, no archived code executed or hardware access.','entries':records};(out/'archive-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'files':len(records),'bytes':sum(r['bytes']for r in records),'root':str(root)},ensure_ascii=False))
for r in records:
 p=PurePosixPath(r['path'])
 if len(p.parts)==2 or p.name in('report.txt','power-input-session.json','comment.txt','power-input-validation.json'):print(r['path'],r['bytes'])
