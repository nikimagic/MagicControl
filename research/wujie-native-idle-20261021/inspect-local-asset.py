from pathlib import Path
from collections import Counter
import math,json
p=Path(r'C:\Users\admin\Documents\mechrevo\Research\BIOS_OTA_Full\BIOS_OTA\assets\00643994.bin');b=p.read_bytes()
for off in [0,0x100,0x1000,0x10000,0x20000,0x30000,len(b)-64]:print(hex(off),b[off:off+64].hex(' '))
print('entropy',-sum(n/len(b)*math.log2(n/len(b))for n in Counter(b).values()))
print('common',Counter(b).most_common(10))
print('quarter_bytes',[Counter(b[i::4]).most_common(5)for i in range(4)])
meta=json.loads(Path(r'C:\Users\admin\Documents\mechrevo\Research\BIOS_OTA_Full\BIOS_OTA\pe.json').read_text());print(str(meta.get('assets',[]))[:1000])
