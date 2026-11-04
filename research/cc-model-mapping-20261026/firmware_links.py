"""Read saved firmware data only. Exact matches are evidence, not compatibility."""
from pathlib import Path
import json,re,hashlib,collections
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];CAT=ROOT/'research/ec-platform-catalogue-20261023'
units=json.loads((CAT/'source-units.json').read_text(encoding='utf8'))
profiles=json.loads((CAT/'charge-project-coverage.json').read_text(encoding='utf8'))['profiles']
targets={p['board'] for p in profiles}|{p['label'] for p in profiles if len(p['label'])>8}
targets|={'GM6IR0C','GM5HG0A','GM6PX7X','GM6BG0X','GM6IX9B','X5SP4NAG','HYDROC-16','X6AR55xY','X6DR55xx-B2','X8xR55xP-B2','X6xR55xY','X6xx57TW','X6xx55xW-B2','GX6HG0U-F'}
patterns=[(t,enc,t.encode(enc))for t in sorted(targets)for enc in ('ascii','utf-16-le') if t.isascii()]
rows=[]
for u in units:
 raw=Path(u['path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==u['sha256']
 hits=[]
 for target,enc,needle in patterns:
  at=raw.lower().find(needle.lower())
  if at>=0:hits.append({'target':target,'encoding':enc,'offset':at,'context_hex':raw[max(0,at-16):at+len(needle)+24].hex()})
 stores=[]
 if u['kind']=='EC':
  for addr in (0x740,0x74c,0x7c3):
   needle=bytes((0x90,addr>>8,addr&255,0x74))
   for m in re.finditer(re.escape(needle),raw):
    at=m.start()
    if at+6<=len(raw) and raw[at+5]==0xf0:stores.append({'address':f'{addr:03X}','file_offset':at,'value':raw[at+4],'bytes':raw[at:at+6].hex(),'scope':'Exact MOV DPTR/MOV A immediate/MOVX bytes. Instruction boundary/reachability not proved solely by pattern.'})
 rows.append({'unit':u['id'],'kind':u['kind'],'source':u['source_label'],'path':u['path'],'sha256':u['sha256'],'exact_identity_string_hits':hits,'literal_write_patterns':stores})
# Decompressed BIOS sections can hold DMI templates absent from compressed image.
cohort=json.loads((CAT/'canonical-bios-dsdt-inventory.json').read_text(encoding='utf8'))['cohort']
sections=[];cache={}
for unit in cohort:
 for p in Path(unit['dump_folder']).rglob('*body.bin'):
  raw=p.read_bytes();h=hashlib.sha256(raw).hexdigest()
  if h not in cache:
   hh=[]
   for target,enc,needle in patterns:
    at=raw.lower().find(needle.lower())
    if at>=0:hh.append({'target':target,'encoding':enc,'offset':at})
   cache[h]=hh
  if cache[h]:sections.append({'container_unit':unit['unit'],'container_source':unit['source'],'section':str(p),'sha256':h,'matches':cache[h]})
result={'scope':'Saved canonical units and selected decompressed body.bin; exact text matches, not automatic runtime/template selection or compatibility','units':rows,'section_hits':sections,'unique_sections_scanned':len(cache),'hardware_execution':False}
(HERE/'firmware-links.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('units',len(rows),'unique sections',len(cache),'section occurrences with identity strings',len(sections))
for r in rows:
 if r['exact_identity_string_hits'] or r['literal_write_patterns']:print(r['source'],[x['target']for x in r['exact_identity_string_hits']],r['literal_write_patterns'])
for r in sections:print(r['container_source'],Path(r['section']).name,[x['target']for x in r['matches']])
