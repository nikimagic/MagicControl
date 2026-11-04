"""Static local source inventory. Never imports or executes OEM code."""
from pathlib import Path
import subprocess,re,json,hashlib,collections
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).resolve().parent
roots=[ROOT.parent.parent/'mechrevo'/'Decompiled',ROOT/'research/devices',ROOT/'research/new-l16-20261022/decompiled',ROOT/'build']
paths=[]
for root in roots:
 if root.exists():
  paths += subprocess.run(['rg','--files','-uuu',str(root),'-g','*.cs','-g','*.il'],capture_output=True,text=True,encoding='utf-8').stdout.splitlines()
pattern=re.compile(r'(?<![A-Za-z0-9])(?:G[KMIX][45678][A-Za-z0-9_xX-]{2,18}|PH[46xX][A-Za-z0-9_]{2,18}|PF[456][A-Za-z0-9_]{2,18}|X[45678xX][A-Za-z0-9_xX-]{3,18}|HLYL-WXX9(?:-PCB)?|HYDROC[- ]16(?:[- ]G2)?)(?![A-Za-z0-9])')
hits=collections.defaultdict(list);enums=collections.defaultdict(list);bios_enums=collections.defaultdict(list);seen=set();files=[];errors=[]
for name in paths:
 p=Path(name)
 # Only OEM decompilation/recovery, excluding test/build output and our application.
 if not any(s in str(p).lower() for s in ('decompiled','decompil','csharp','all-active','recovered')):continue
 try:raw=Path('\\\\?\\'+str(p)).read_bytes()
 except OSError as e:errors.append(str(e));continue
 sha=hashlib.sha256(raw).hexdigest()
 if sha in seen:continue
 seen.add(sha);txt=raw.decode('utf-8-sig',errors='replace');files.append({'path':str(p),'sha256':sha,'bytes':len(raw)})
 for n,line in enumerate(txt.splitlines(),1):
  # Accept model-shaped symbols or literal strings, preserve line for review.
  for token in set(pattern.findall(line)):
   hits[token].append({'path':str(p),'line':n,'text':line.strip()[:650]})
 for match in re.finditer(r'enum\s+(ProjectID|BIOS_PROJECT_ID)\s*(?::[^\{]+)?\s*\{([^}]+)\}',txt):
  value=-1
  body=re.sub(r'//[^\n]*','',match[2])
  for member in body.split(','):
   m=re.fullmatch(r'\s*(\w+)\s*(?:=\s*(0x[\da-fA-F]+|\d+))?\s*',member)
   if not m:continue
   value=int(m[2],0) if m[2] else value+1
   (enums if match[1]=='ProjectID' else bios_enums)[m[1]].append({'value':value,'path':str(p),'line':txt.count('\n',0,match.start())+1})
data={'roots':[str(x) for x in roots],'unique_source_files':len(files),'files':files,'project_enums':dict(enums),'bios_project_enums':dict(bios_enums),'model_candidates':dict(sorted(hits.items())),'read_errors':errors,'scope':'All selected local C#/IL decompilation/recovery text; enum families are not individual retail laptops; protected/native/encoded identities may remain unresolved.'}
(OUT/'source-inventory.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
print('unique files',len(files),'enum members',len(enums),'candidate names',len(hits),'errors',len(errors))
for k,v in sorted(hits.items()):print(k,len(v),v[0]['text'][:130])
