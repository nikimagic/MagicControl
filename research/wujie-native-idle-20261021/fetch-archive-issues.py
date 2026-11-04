from pathlib import Path
import subprocess,json,hashlib,concurrent.futures
out=Path('research/wujie-native-idle-20261021/sources')
urls={
'archive-availability':'https://archive.org/wayback/available?url=ota.mechrevo.com%3A2080%2FbiosUp%2FD9D24AA7%2Fmro%2FD9D24AA7mro.json',
'archive-cdx-model':'https://web.archive.org/cdx/search/cdx?url=ota.mechrevo.com%3A2080%2FbiosUp%2FD9D24AA7%2F%2A&output=json&filter=statuscode%3A200&collapse=urlkey',
'github-issues-board':'https://api.github.com/search/issues?q=%22X5SP4NAG%22&per_page=30',
'github-issues-version':'https://api.github.com/search/issues?q=%22N.1.20MRO15%22&per_page=30'}
def read(item):
 name,url=item;p=out/(name+'.body');r=subprocess.run(['curl.exe','--silent','--show-error','--location','--connect-timeout','5','--max-time','18','--max-filesize','2000000','--output',str(p),'--write-out','%{http_code}',url],capture_output=True,timeout=23)
 row={'url':url,'file':str(p),'status':r.stdout.decode(),'exit_code':r.returncode,'error':r.stderr.decode(errors='replace'),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()if p.exists()else None};print(json.dumps(row),flush=True);return row
with concurrent.futures.ThreadPoolExecutor(max_workers=3)as pool:rows=list(pool.map(read,urls.items()))
(out/'archive-github-requests.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
