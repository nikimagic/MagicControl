from pathlib import Path
import subprocess,json,hashlib,concurrent.futures
out=Path('research/wujie-native-idle-20261021/sources');rev='3a574dafe837571cc3176b4ed452a3fd84b3cd24'
paths=['README.md','docs/FIRMWARE_ANALYSIS_REPORT.md','docs/firmware_disassembly.md','docs/readable_firmware.md','tools/ec_registers.tsv','tools/ec_functions.tsv']
def read(path):
 url=f'https://raw.githubusercontent.com/minortex/ec_reverse/{rev}/{path}';p=out/('related-'+path.replace('/','-'));r=subprocess.run(['curl.exe','--silent','--show-error','--location','--connect-timeout','5','--max-time','15','--max-filesize','200000','--output',str(p),'--write-out','%{http_code}',url],capture_output=True,timeout=20)
 row={'url':url,'file':str(p),'status':r.stdout.decode(),'exit_code':r.returncode,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()if p.exists()else None};print(json.dumps(row),flush=True);return row
with concurrent.futures.ThreadPoolExecutor(max_workers=3)as pool:rows=list(pool.map(read,paths))
(out/'related-request-manifest.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
