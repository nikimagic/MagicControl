from pathlib import Path
import json
out=Path('research/wujie-native-idle-20261021');p=out/'saved-ram-idle-screen.json';r=json.loads(p.read_text());k=r['keyboard_event_neighborhoods'][0]
for c in r['byte_ramp_candidates']:
 n=next((x for x in k['changed_bytes']if x['address']==c['address']),None);c['at_keyboard_input']=n;c['interpretation']='Continues through observed keypress without resetting; linear match over one post-key interval also fits a free-running counter. Not an identified keyboard idle counter.'
 print(c['address'],n)
r['conclusion']='Both byte ramp matches continue through keyboard input without resetting. No confirmed keyboard-idle timer/setting found. Single measured idle cycle is insufficient for causal attribution; screening excludes only simple direct byte ramp encodings, not other firmware designs or private XRAM.'
p.write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
