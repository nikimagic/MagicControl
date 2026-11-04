"""Saved WUJIE EC, OEM source/profile and portable log comparison. No hardware."""
from pathlib import Path
import hashlib,json,re,sys
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
BASE=Path('C:/Users/admin/Documents/mechrevo')
reference=BASE/'Decompiled/GCUService_Reference_w568w/GCUService/MyControlCenter/MyFan/MyFanManager_RamFan1p5.cs'
profiles=BASE/'ControlCenter-Before-OEM-Reinstall-20260929/Installed-5.57.1.2/AiStoneService/MyControlCenter/UserPofiles'
inputs=[ROOT/'src/core/include/MagicControl/Performance.h',ROOT/'src/gui/common/CpuPowerLabels.h',
        ROOT/'src/platform/windows/ApuPowerPolicyWindows.h',ROOT/'src/protocols/uniwill/x5sp4nag/Performance.cpp',reference,
        BASE/'Decompiled/FanManager/MyControlCenter.MyFan.MyFanManager_RamFan1p5.decompiled.cs',
        ROOT/'tests/test_util/analysis/charger-rating-smu-20261016/REPORT.md',
        ROOT/'tests/test_util/analysis/battery-platform-smu-20261016/ALL-MODES.md']
metadata=[]
def record(path):
    data=path.read_bytes();metadata.append({'path':str(path),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()});return data
for path in inputs:record(path)
rows=[]
for path in sorted(profiles.glob('Mode*_Profile*.json')):
    j=json.loads(record(path).decode('utf-8-sig'));cpu=j.get('CPU',{})
    rows.append({'file':path.name,'name':j.get('Name'),'activated':j.get('Activated'),
                 'AmdSPL_SPPT_FPPT':[cpu.get(k) for k in ['AmdSPL','AmdSPPT','AmdFPPT']],
                 'fan':j.get('FAN'),'scope':'Saved user-profile backup, not independently pristine factory data.'})
sys.path.insert(0,str(ROOT/'build/honor-analysis'))
from disasm51 import addresses,codeanalyzer,instructions
firmware=ROOT/'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin'
data=record(firmware);assert metadata[-1]['sha256']=='5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8'
rom=data[:0x8000]+data[0x18000:0x20000]
decode=codeanalyzer.CodeAnalyzer(instructions.Instructions(),rom,addresses.Addresses(),0,True,[],set())._CodeAnalyzer__disassemble_instruction
windows=[]
for start,end in [(0x9e99,0x9edb),(0xd74e,0xd75c),(0x9ba6,0x9c1b),(0xb9b3,0xb9c8),(0xd9f8,0xda02),(0x8d66,0x8e30)]:
    pc=start;lines=[f'WUJIE bank2 CODE{start:04X}..{end:04X}']
    while pc<end:
        text,n=decode(pc);lines.append(f'{pc:04X} {rom[pc:pc+n].hex():<10} {text.strip()}');pc+=n
    windows.append('\n'.join(lines))
(OUT/'FIRMWARE-WINDOWS.asm').write_text('\n\n'.join(windows)+'\n',encoding='utf8')
logrows=[]
log=ROOT/'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/MagicControl-3.jsonl'
raw=record(log)
for line in raw.splitlines():
    try:r=json.loads(line)
    except ValueError:continue
    m=r.get('message','')
    if isinstance(m,str) and 'SETs APU:' in m:
        logrows.append({'utc':r['utc'],'version':r.get('app_version'),'action_id':r.get('action_id'),'message':m})
    if r.get('channel')=='battery_candidates' and isinstance(m,str) and 'SNAPSHOT' in m:
        fields=dict(re.findall(r'(0x[0-9A-Fa-f]+)=0x([0-9A-Fa-f]+)/\d+',m))
        keep={a:int(fields[a],16) for a in ['0x7C3','0x770','0x7CE','0x783','0x784','0x785','0x730','0x731','0x732','0x734','0x735','0x736','0x7A7','0x7A8','0x7A9'] if a in fields}
        if keep and (not logrows or logrows[-1].get('fields')!=keep):logrows.append({'utc':r['utc'],'fields':keep})
(OUT/'EVIDENCE.json').write_text(json.dumps({'scope':'Saved files only; no OEM execution, app or hardware calls.',
    'inputs':metadata,'profiles_557':rows,'log_observations':logrows},ensure_ascii=False,indent=2),encoding='utf8')
print('OEM557 saved profiles',len(rows))
for row in rows:
    if row['file'].endswith('Profile1.json'):print(row['file'],row['AmdSPL_SPPT_FPPT'])
for row in logrows[-8:]:print(row)
