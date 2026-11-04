from pathlib import Path
import concurrent.futures,subprocess,json,hashlib,datetime
OUT=Path('research/wujie-native-idle-20261021/sources')
urls={
'ota-http':'http://ota.mechrevo.com:2080/biosUp/D9D24AA7/mro/D9D24AA7mro.json',
'ota-https':'https://ota.mechrevo.com:2080/biosUp/D9D24AA7/mro/D9D24AA7mro.json',
'ota-legacy':'http://112.80.53.142:3080/biosUp/D9D24AA7/mro/D9D24AA7mro.json',
'ota-dns':'https://dns.google/resolve?name=ota.mechrevo.com&type=A',
'official-catalog-first':'https://gwapi.mechrevo.com/api/v1/client/drivers?limit=500&page=1',
'xmg-e25':'https://download.schenker-tech.de/package/xmg-evo-14-15-e25-xev14ae25-xev15ae25/',
'ec-reverse-tree':'https://api.github.com/repos/minortex/ec_reverse/git/trees/main?recursive=1',
}
def fetch(item):
 name,url=item;target=OUT/(name+'.body');headers=OUT/(name+'.headers.txt')
 p=subprocess.run(['curl.exe','--silent','--show-error','--location','--connect-timeout','6','--max-time','20','--max-filesize','8000000','--dump-header',str(headers),'--output',str(target),'--write-out','%{http_code}',url],capture_output=True,timeout=25)
 r={'name':name,'url':url,'exit_code':p.returncode,'http_status':p.stdout.decode(errors='replace'),'error':p.stderr.decode(errors='replace'),'fetched_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 if target.exists():r.update(bytes=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest())
 (OUT/(name+'.request.json')).write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8');print(json.dumps(r),flush=True);return r
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(fetch,urls.items()))
(OUT/'requests.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
