import subprocess,pathlib

def staged(path, replacements):
 text=subprocess.check_output(['git','show','HEAD:'+path]).decode('utf-8')
 for before,after in replacements:
  if text.count(before)!=1:raise RuntimeError('Nonunique replacement: '+path+' '+repr(before[:60]))
  text=text.replace(before,after,1)
 oid=subprocess.check_output(['git','hash-object','-w','--stdin'],input=text.encode('utf-8')).decode().strip()
 subprocess.run(['git','update-index','--cacheinfo','100644',oid,path],check=True)

staged('src/platform/windows/CMakeLists.txt', [('    PawnTelemetryWindows.cpp\n','    PawnTelemetryWindows.cpp\n    BatteryChargeWindows.cpp\n')])
p='src/gui/windows/SettingsPage.cpp'
current=pathlib.Path(p).read_text(encoding='utf-8')
head=subprocess.check_output(['git','show','HEAD:'+p]).decode('utf-8')
start='    void writeBattery(int preset) {'
end='    void writePerformance(int preset) {'
old=head[head.index(start):head.index(end)]
new=current[current.index(start):current.index(end)]
staged(p,[
 ('#include "../../platform/windows/FanControlWindows.h"\n','#include "../../platform/windows/FanControlWindows.h"\n#include "../../platform/windows/BatteryChargeWindows.h"\n'),
 ('        std::uint8_t battery{};\n','        std::uint8_t battery{};\n        std::uint8_t batteryGate{}, batteryPlatform{};\n'),
 ('        return index < 0 ? L"EC не сообщил выбранный предел заряда." :\n                           L"Выбранный верхний предел заряда: " + std::wstring(limits[index]);',
 '        if (index < 0) return L"EC не сообщил выбранный предел заряда.";\n        const std::wstring suffix = index == 0 ? L" · без ограничения" :\n            (state.batteryGate & 4) ? L" · ограничение включено EC" : L" · включение не подтверждено EC";\n        return L"Выбранный верхний предел заряда: " + std::wstring(limits[index]) + suffix;'),
 ('        next.battery = ec->readStable(ec::kBatteryChargeControl);\n','        next.battery = ec->readStable(ec::kBatteryChargeControl);\n        next.batteryGate = ec->readStable(0x742);\n        next.batteryPlatform = ec->readStable(0x7C3);\n'),
 (old,new)])
subprocess.run(['git','add','--','AGENTS.md','docs/BATTERY-CHARGE-LIMIT.txt','src/platform/windows/BATTERY.md','src/platform/windows/BatteryChargeWindows.cpp','src/platform/windows/BatteryChargeWindows.h','src/platform/windows/ConfigurationRestoreWindows.cpp','src/platform/windows/PowerDiagnosticsWindows.cpp'],check=True)
