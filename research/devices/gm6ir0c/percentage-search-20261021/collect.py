"""Offline evidence collector. Reads source files; never imports investigated code."""
import hashlib
import json
from pathlib import Path
import re
import uuid

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
OUT = REPO / 'out' / HERE.relative_to(REPO) / ('run-' + uuid.uuid4().hex); OUT.mkdir(parents=True, exist_ok=False)
REF = REPO.parent / 'Reference_Projects'
OLD = Path(r'C:\Users\admin\Documents\mechrevo')
OEM = REPO / '../Decompile/GM6IR0C/Control_Center'
HYDRO = REPO / '../Reference_Projects/Battery_Temperature_20261020/hydrocontrol/HydroControl-fc4024ce'

entries = [
    ('linux saved snapshot', OLD/'Research/Uniwill-Pulsation-20261014/uniwill-acpi.c', [(1869,1878),(1995,2019),(2121,2147),(2424,2435),(2547,2558),(2679,2713),(3348,3356)]),
    ('linux newly pinned upstream', HERE/'sources/linux-uniwill-acpi.c', [(1995,2019),(2121,2147),(2679,2713),(3348,3356)]),
    ('DKMS independent fork', REPO/'../Reference_Projects/Battery_Temperature_20261020/uniwill-laptop-dkms-main/uniwill-acpi.c', [(2632,2660),(2686,2704),(3057,3072),(3126,3139),(3195,3252)]),
    ('Tuxedo native profiles', REPO/'../Reference_Projects/Battery_Temperature_20261020/tuxedo-drivers-main/src/uniwill_keyboard.h', [(708,770)]),
    ('Luma real backend', REF/'LumaDesk/repository/src/core/hal/windows.rs', [(442,493)]),
    ('Luma simulation backend', REF/'LumaDesk/repository/src/core/hal/mock.rs', [(210,230)]),
    ('Luma verification scope', REF/'LumaDesk/repository/scripts/verify-controls.py', [(125,132)]),
    ('Luma target identity', REF/'LumaDesk/repository/reverse/native/REPORT.md', [(1,7),(59,69)]),
    ('FanSlider broad experiment', REF/'FanSlider/repository/Program.cs', [(914,936)]),
    ('OpenRevo source boundaries', REF/'OpenRevo/repository/README_en.md', [(9,15),(106,118)]),
    ('NUCtool target', REF/'NUCtool/repository/README_en-US.md', [(25,30)]),
    ('NUCtool active percentage caller', REF/'NUCtool/repository/src-tauri/src/lib.rs', [(693,734)]),
    ('NUCtool DLL loader/wrappers', REF/'NUCtool/repository/src-tauri/src/acpi/uniwillwcf.rs', [(1,9),(122,126),(184,204),(329,344)]),
    ('NUCtool unused direct EC setter', REF/'NUCtool/repository/src-tauri/src/acpi/uniwillacpi.rs', [(391,405)]),
    ('NUCtool DLL metadata contract', HERE/'nuctool-decompiled/IUniwillService.cs', [(255,312)]),
    ('NUCtool feature metadata', HERE/'nuctool-decompiled/IntelSDK/FeatureType.cs', [(40,60)]),
    ('NUCtool mode metadata', HERE/'nuctool-decompiled/IntelSDK/BatteryMode.cs', [(1,20)]),
    ('NUCtool failed method body decode', HERE/'nuctool-decompiled/UniwillServiceClient.cs', [(1004,1044)]),
    ('OEM manager dispatcher', OEM/'020002B9-MyControlCenter.MyFanCtrl.cs', [(27,72)]),
    ('OEM QC model capability', OEM/'0200022C-MyControlCenter.CustomizeCtrl.cs', [(259,316)]),
    ('OEM QC setter', OEM/'02000254-MyControlCenter.MyFanManager_QC.cs', [(548,561),(867,889),(1808,1832)]),
    ('OEM selected protection', OEM/'020001C6-GCUService.MySystem.BatteryProtection2.cs', [(140,178),(278,373)]),
    ('OEM missing UEFI byte dispatch', OEM/'0200023A-MyControlCenter.NvramVariable.cs', [(313,369)]),
    ('exact GM6 AML fields/BST', REPO/'research/research-gm6ir0c/samples/DSDT.dsl', [(134395,134412),(135385,135412)]),
    ('Hydro script declaration and caveats', HYDRO/'charge_path_probe.py', [(1,30),(290,324)]),
    ('Hydro raw stop60 capture', HYDRO/'threshold60-stopped.json', [(1,82)]),
    ('Hydro bank correction', HYDRO/'ec_disasm.py', [(25,43),(85,96)]),
]

files=[]
for title,path,ranges in entries:
    row={'title':title,'path':str(path),'exists':path.is_file()}
    if path.is_file():
        b=path.read_bytes();lines=b.decode('utf-8-sig',errors='replace').splitlines()
        row.update(bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),excerpts=[])
        for lo,hi in ranges:
            row['excerpts'].append({'start':lo,'end':hi,'lines':[
                {'line':i+1,'text':line[:700]} for i,line in enumerate(lines) if lo<=i+1<=hi]})
    files.append(row)

searches=[]
for expression in [r'\bSetBatteryChargingLimit_Up\s*\(',r'\bSetBatteryChargingLimit_Down\s*\(',r'\bMyFanManager_QC\b',r'\bIUniwillService\b',r'\bSetBatteryChargingLevel\s*\(']:
    matches=[]
    for p in sorted(OEM.glob('*.cs')):
        for number,line in enumerate(p.read_text(encoding='utf-8-sig',errors='replace').splitlines(),1):
            if re.search(expression,line):matches.append({'path':str(p),'line':number,'text':line[:500]})
    searches.append({'scope':str(OEM),'expression':expression,'matches':matches})

binary=REF/'NUCtool/repository/src-tauri/dll/NUCToolV2.dll'
data=binary.read_bytes()
binary_meta={'path':str(binary),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'executed':False,
             'decompiler':'Existing local ilspycmd.exe -p (offline parse only)',
             'decompile_exit_code':1,'limitations':'Four reported errors plus invalid method bodies; metadata interfaces are available, implementation trace is not recovered'}

repos=[]
for name in ['LumaDesk','FanSlider','OpenRevo','NUCtool']:
    p=REF/name/'download-manifest.json'
    d=json.loads(p.read_text(encoding='utf-8-sig'))
    repos.append({k:d.get(k) for k in ['project','repository','commit','downloadedUtc','license']})

stop=json.loads((HYDRO/'threshold60-stopped.json').read_text(encoding='utf-8-sig'))['singles']
observed={'upper_raw':stop['0x07B9'],'reached_bit7':bool(stop['0x07B9']&0x80),
          'platform':stop['0x07C3'],'gate_raw':stop['0x0742'],'gate_bit2':bool(stop['0x0742']&4),
          'rsoc':stop['0x04AB'],'actual_current_raw_le':stop['0x0434']+(stop['0x0435']<<8),
          'voltage_raw_le':stop['0x0438']+(stop['0x0439']<<8),
          'scope':'Foreign HYDROC G1 capture, not GM6 compatibility proof'}

result={'date':'2026-10-21','method':'Readonly source/hash/metadata analysis. No investigated executable, hardware call, tests, build, settings or production mutation.',
        'repositories':repos,'files':files,'bounded_oem_searches':searches,'nuctool_binary':binary_meta,'hydro_stop_capture':observed}
(OUT/'evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f'{len(files)} source entries; {len(searches)} bounded searches; binary not executed')
