from pathlib import Path,PurePosixPath
import hashlib,json,zipfile,stat,uuid
source=Path('Users_logs/logs_GM6IR0C (5).zip')
dest=(Path('out/gm6ir0c-logs5-20261021')/('extract-'+uuid.uuid4().hex)).resolve();dest.mkdir(parents=True,exist_ok=False)
out=dest
digest=lambda b:hashlib.sha256(b).hexdigest()
manifest={'archive':source.name,'sha256':digest(source.read_bytes()),'bytes':source.stat().st_size,'files':[]}
with zipfile.ZipFile(source)as z:
 assert sum(i.file_size for i in z.infolist())<500_000_000
 for info in z.infolist():
  name=PurePosixPath(info.filename.replace('\\','/'))
  assert not name.is_absolute()and '..'not in name.parts and ':'not in str(name)
  assert not stat.S_ISLNK(info.external_attr>>16)
  target=dest.joinpath(*name.parts).resolve();assert target.is_relative_to(dest)
  if info.is_dir():continue
  data=z.read(info);target.parent.mkdir(parents=True,exist_ok=True)
  if target.exists():assert target.read_bytes()==data
  else:target.write_bytes(data)
  manifest['files'].append({'path':str(name),'bytes':len(data),'sha256':digest(data)})
(out/'archive-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in manifest.items()if k!='files'},ensure_ascii=False))
print('files',len(manifest['files']),'uncompressed',sum(x['bytes']for x in manifest['files']))
print('top-level',[(p.name,p.stat().st_size)for p in dest.glob('*/*')if p.is_file()])
