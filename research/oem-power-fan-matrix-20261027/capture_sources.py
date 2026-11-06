"""Read saved C# only. No application, driver or hardware execution."""
from pathlib import Path
import hashlib, json

root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
reference = Path('C:/Users/admin/Documents/mechrevo/Decompiled/GCUService_Reference_w568w/GCUService')
g5 = root/'research/devices/gm5hg0a/oem-5.52.49.6/laptop-control/csharp'
g6 = root/'../Decompile/GM6IR0C/Control_Center'
sources = {
 'WUJIE_EXACT_PROTECTED': Path('C:/Users/admin/Documents/mechrevo/Decompiled/FanManager/MyControlCenter.MyFan.MyFanManager_RamFan1p5.decompiled.cs'),
 'REFERENCE_MANAGER': reference/'MyControlCenter/MyFan/MyFanManager_RamFan1p5.cs',
 'REFERENCE_FAN_TABLE': reference/'MyControlCenter/MyFan/FanTable/MyFanTableCtrl.cs',
 'REFERENCE_POWER': reference/'MyControlCenter/PowerModeEvent.cs',
 'REFERENCE_EC': reference/'MyECIO/MyEcCtrl.cs',
 'REFERENCE_BATTERY': reference/'MySystem/BatteryProtection2.cs',
 'GM5_MANAGER': g5/'GCUService/MyControlCenter.MyFan.MyFanManager_RamFan1p5.cs',
 'GM5_FAN_TABLE': g5/'GCUService/MyControlCenter.MyFan.FanTable.MyFanTableCtrl.cs',
 'GM5_POWER': g5/'GCUService/MyControlCenter.PowerModeEvent.cs',
 'GM5_EC': g5/'DefaultTool/MyECIO.MyEcCtrl.cs',
 'GM5_BATTERY': g5/'GCUService/GCUService.MySystem.BatteryProtection2.cs',
 'GM6_MANAGER': g6/'020003F0-MyControlCenter.MyFan.MyFanManager_RamFan1p5.cs',
 'GM6_FAN_TABLE': g6/'0200041C-MyControlCenter.MyFan.FanTable.MyFanTableCtrl.cs',
 'GM6_POWER': g6/'02000331-MyControlCenter.PowerModeEvent.cs',
 'GM6_EC': g6/'020000EC-MyECIO.MyEcCtrl.cs',
 'GM6_BATTERY': g6/'020001C6-GCUService.MySystem.BatteryProtection2.cs',
 'WUJIE_MEASUREMENTS': root/'tests/test_util/analysis/charger-rating-smu-20261016/REPORT.md',
}
needles = ('void ModeSwitchChanged(', 'void PowerStatusChange(', 'void SetUserProfile(',
 'void SetFanMode(', 'void UserSet_Mode1(', 'void UserSet_Mode2(', 'void UserSet_Mode3(',
 'void UserSet_Mode4(', 'void Init(', 'uint GetDefaultMode(', 'void SetFanTable(',
 'void SetFanControlByRamFan1p5(', 'void RefreshACLineStatus(', 'int GetTurboModeSupport(',
 'int GetTypeCsatusFromEC(', 'void SetHealthProtectionHigh(', 'void SetHealthProtectionMiddle(',
 'void SetHealthProtectionLow(', 'int GetFanSafetyProtectStatus(')
manifest = []
for key, path in sources.items():
    data = path.read_bytes()
    lines = data.decode('utf-8-sig').splitlines()
    found = [{'line': i+1, 'signature': line.strip()} for i,line in enumerate(lines)
             if any(n in line for n in needles)]
    manifest.append(dict(id=key, path=str(path), sha256=hashlib.sha256(data).hexdigest(),
                         bytes=len(data), methods=found))
    target = out/(key+'.txt')
    target.write_text(str(path)+'\n'+ '\n'.join(f'{i+1:05d} {line}' for i,line in enumerate(lines))+'\n', encoding='utf-8')
(out/'EVIDENCE.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n', encoding='utf-8')
print('Saved', len(manifest), 'numbered source copies and SHA256 manifest; no hardware access')
