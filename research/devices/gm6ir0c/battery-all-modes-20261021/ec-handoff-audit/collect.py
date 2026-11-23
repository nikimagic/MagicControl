"""Saved-image and saved-log evidence only. No hardware APIs, app or firmware execution."""
from pathlib import Path
import hashlib,json,re,uuid
root=Path(__file__).resolve().parents[5]
run=root/'out/gm6-battery-firmware-20261021'/('ec-handoff-'+uuid.uuid4().hex)
out=run/'ec-handoff-audit';out.mkdir(parents=True,exist_ok=False)
(run/'bios-audit').mkdir()
w=root/'research/gm6-battery-firmware-20261021';path=w/'EC-MRO10-v1.12.bin';b=path.read_bytes();sha=lambda raw:hashlib.sha256(raw).hexdigest()
assert sha(b)=='5659c8217e9d93d3fd6fe278756872d7c5adbd15a0687d35705e2df4bb5c05dd'
old=(w/'EC-MRO09-candidate.bin').read_bytes()
def at(logical,bank):return logical if logical<0x8000 else 0x8000+bank*0x8000+(logical-0x8000)
def rec(logical,bank,size):
 p=at(logical,bank);raw=b[p:p+size]
 return {'logical':logical,'bank':bank,'physical':p,'bytes':size,'sha256':sha(raw),'hex':raw.hex(),
  'identical_bytes_in_EC111_at_same_offset':old[p:p+size]==raw}
known={
 'normal_charger_poll_C89A':rec(0xc89a,0,0x74),
 'charger_write_dispatch_C929':rec(0xc929,0,0xa4),
 'wire_driver_8809':rec(0x8809,0,0x19a),
 'command_buffer_CF84':rec(0xcf84,0,15),
 'high_byte_buffer_CFAE':rec(0xcfae,0,9),
 'common_dispatch_1C06':rec(0x1c06,0,6),'common_dispatch_1C60':rec(0x1c60,0,6),
 'current_pending_C086':rec(0xc086,1,9),'stop_current_B37C':rec(0xb37c,1,17),
 'budget_current_B35F':rec(0xb35f,1,29),'publish_budget_B71D':rec(0xb71d,1,10),
 'voltage_DE0F':rec(0xde0f,1,0x81),'voltage_pending_DE6B':rec(0xde6b,1,0x14),
 'current_pending_DE7F':rec(0xde7f,1,0x11),
 'voltage_ISDB_choice_B338':rec(0xb338,1,0x1c),'ISDB_getter_C102':rec(0xc102,2,0x0a),
 'ISDB_raw_getter_DC26':rec(0xdc26,2,7),'common_ISDB_1936':rec(0x1936,0,6),
 'copy_BMS_targets_DCBA':rec(0xdcba,1,0x24),
 'platform4_helper_D827':rec(0xd827,2,7),
 'fan_pointer_branch_880B':rec(0x880b,2,0x1e),'fan_pointer_branch_883F':rec(0x883f,2,0x1a),
 'native_PL_branch_9A00':rec(0x9a00,2,0x42),'keyboard_Fn_admission_B918':rec(0xb918,0,0x27),
 'Fn_toggle_BB29':rec(0xbb29,0,0x0c),'input_private_counter_B571':rec(0xb571,2,0x32)}
platform=[]
for m in re.finditer(re.escape(bytes.fromhex('9007c3')),b):
 p=m.start();bank=None if p<0x8000 else p//0x8000-1;logical=p if p<0x10000 else 0x8000+p%0x8000
 platform.append({'physical':p,'bank':bank,'logical':logical,'context_offset':max(0,p-8),'context_hex':b[max(0,p-8):p+40].hex()})
helper_calls=[{'physical':m.start(),'logical':m.start()-0x10000,'context_hex':b[m.start()-6:m.start()+16].hex()}
 for m in re.finditer(re.escape(bytes.fromhex('12d827')),b) if m.start()>=0x18000]
tables=[]
for pointer in [0x5d78,0x5d90,0x5e08,0x5df0]:
 raw=b[pointer:pointer+24];pairs=[int.from_bytes(raw[n:n+2],'big') for n in range(0,24,2)]
 tables.append({'pointer':pointer,'bytes_hex':raw.hex(),'table_pointers':pairs,
  'first_CPU_GPU_tables':[{'pointer':x,'sha256':sha(b[x:x+48]),'Up':list(b[x:x+16]),'Down':list(b[x+16:x+32]),'DutyRaw':list(b[x+32:x+48])} for x in pairs[:2]]})
pl_tables=[{'pointer':p,'raw_47_bytes':list(b[p:p+47]),'hex':b[p:p+47].hex()}for p in [0x6149,0x6178]]
spis=[]
for name in ['backup.fd','bios.bin','new.fd']:
 p=w/'mro10-static'/name;raw=p.read_bytes();offset=raw.find(b);assert offset>=0 and raw.find(b,offset+1)<0
 spis.append({'path':str(p.relative_to(root)),'bytes':len(raw),'sha256':sha(raw),'EC_offset':offset,'EC_bytes':len(b),'EC_sha256':sha(raw[offset:offset+len(b)])})
sig=[]
for name,folder in [('m10-fid-body.bin','MRO10_FID'),('m10-sgn-body.bin','MRO10_SGN'),('m10-smbios-body.bin','MRO10_SMBIOS')]:
 p=root/'../Decompile/GM6IR0C'/folder/'body.bin';raw=p.read_bytes();sig.append({'path':str(p.relative_to(root)),'bytes':len(raw),'sha256':sha(raw),'hex':raw.hex(),
  'strings':[{'offset':m.start(),'text':m.group().decode('ascii')}for m in re.finditer(rb'[\x20-\x7e]{5,}',raw)]})
dump=root/'research/gm6ir0c-logs4-20261021/logs_GM6IR0C/dumps/dump-20261021-175302-0';raw=(dump/'ec.bin').read_bytes();valid=(dump/'ec-valid.bin').read_bytes()
selected={}
for address,width in [(0x454,1),(0x740,1),(0x491,1),(0x30e,2),(0x31a,2),(0x520,2),(0x522,2),(0x7c3,1),(0x770,1)]:
 assert all(valid[address:address+width]);data=raw[address:address+width]
 selected[f'0x{address:03X}']={'hex':data.hex(),'little':int.from_bytes(data,'little'),'big':int.from_bytes(data,'big')}
project_init=[m.start()for m in re.finditer(re.escape(bytes.fromhex('9007407419f0')),b)]
version_init=[m.start()for m in re.finditer(re.escape(bytes.fromhex('e4909ccc93900454f0')),b)]
identity={'scope':'Same family/Project25/ECversion1.12 and live state ABI; no complete live-flash SHA comparison.',
 'image':{'path':str(path.relative_to(root)),'bytes':len(b),'sha256':sha(b)},'SPI_sources':spis,'signature_sections':sig,
 'project25_init_offsets':project_init,'version_init_offsets':version_init,'version_constant':rec(0x9ccc,1,2),
 'EC_version_major':b[0x11ccc]>>6,'EC_version_minor':b[0x11ccc]&63,
 'saved_state':{'path':str((dump/'ec.bin').relative_to(root)),'sha256':sha(raw),'valid_sha256':sha(valid),'selected_fields':selected},
 'patches_metadata':{'path':str((w/'mro10-static/patches.txt').relative_to(root)),
  'sha256':sha((w/'mro10-static/patches.txt').read_bytes()),'note':'Listed patches address Setup/other BIOS GUIDs; equality proves EC bytes unchanged across archive variants, not OEM authenticity of every variant.'}}
(out/'evidence.json').write_text(json.dumps({'image':identity['image'],'known_code':known,'platform_literal_read_sites':platform,
 'platform4_helper_direct_callers':helper_calls,'fan_pointer_tables':tables,'native_power_pointer_tables':pl_tables,
 'limits':['Ghidra8051 C prototypes/carry/register arguments are heuristic. Raw instructions and saved bytes are authoritative.',
 'Some exploratory platform exports begin within blocks or include compiler inline switch data; never infer behavior from their pseudocode alone.',
 'No SMBus capture, ACK measurement, physical charger model, timing or full live EC flash hash acquired.']},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(out.parent/'bios-audit/MRO10-EC112-identity.json').write_text(json.dumps(identity,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
sources=[{'title':'Smart Battery Charger Specification revision1.1,1998-12-11','url':'https://www.sbs-forum.org/specs/sbc110.pdf',
 'retrieved':'2026-10-22','sections':['5.1.1','5.1.2','6.2.3'],'supports':'WriteWord14 current mA,15 voltage mV; zero request terminates charging; interface-level semantics, not chip identification.'},
 {'title':'TI bq24780S datasheet SLUSC27C,revC2017-03','url':'https://www.ti.com/lit/ds/symlink/bq24780s.pdf',
 'retrieved':'2026-10-22','sections':['7.5.1','Table4','7.6.9','7.6.10'],
 'supports':'Comparison only: address12,commands14/15 and3B/3D/3F compatible. ManufacturerID/DeviceID, actual IC and quantization not confirmed on GM6.'}]
(out/'external-primary-sources.json').write_text(json.dumps(sources,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'EC_version':(identity['EC_version_major'],identity['EC_version_minor']),'project_init':project_init,
 'SPI_EC_offsets':[r['EC_offset']for r in spis],'platform_literals':len(platform),'platform4_callers':len(helper_calls),'saved_fields':selected},ensure_ascii=False,indent=2))
