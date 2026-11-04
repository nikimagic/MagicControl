"""Restore existing shared lower-limit code from Git, preserving new UI/voltage work.
Source editing only. No application, EC, driver or firmware execution.
"""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
relative='src/platform/windows/NativeBatteryWindows.cpp'
path=ROOT/relative
original=subprocess.check_output(['git','show','70897c7:'+relative],cwd=ROOT).decode('utf8')
current=path.read_text(encoding='utf-8-sig')
def replace_span(text,old,start,end):
 a=text.index(start);b=text.index(end,a)
 x=old.index(start);y=old.index(end,x)
 return text[:a]+old[x:y]+text[b:]
current=replace_span(current,original,'    void applyExperimental(','    void rememberThresholds(')
current=replace_span(current,original,
 '                if(saved[DeviceSetting::ChargeAuto]==2',
 '                restoreStatus.clear();')
current=current.replace('if(wujie||saved[DeviceSetting::BatteryPercentageEnabled]==0){','if(wujie){',1)
for message in ('Нижний порог не записывается','Lower threshold writer retired'):
 current=current.replace('        if(address!=experimentalUpper)throw std::runtime_error("'+message+'");\n','')
marker='        patch[DeviceSetting::BatteryCardView]=1;\n'
assert marker in current
current=current.replace(marker,'        if(pair.restart&thresholdMask)patch[DeviceSetting::ChargeRestart]=pair.restart&thresholdMask;\n'+marker,1)
current=replace_span(current,original,
 '            state.percentageWritable=',
 '            if(nativeEcPolicyProfile')
path.write_text(current,encoding='utf8')
header=ROOT/'src/platform/windows/NativeBatteryWindows.h'
text=header.read_text(encoding='utf-8-sig')
text=text.replace('// Exact-board upper100(raw0)/95/85/10. The unknown7D0 is never written.\n    // No WMI/UEFI/platform/profile writes in this upper setter.',
 '// Exact-board trial: upper100(raw0)/95/85/10 and the saved lower parameter.\n    // Native voltage mode is separate. Lower semantics remain explicitly unverified.')
text=text.replace('void setChargeRestart(int percent); // Retired compatibility entry: rejects before hardware access.',
 'void setChargeRestart(int percent); // Shared lower API, model guards/readback/autosave.')
header.write_text(text,encoding='utf8')
assert 'writeThreshold(experimentalRestart,target.restart' in current
assert 'try{applyRestart(*restart,false);}' in current
assert 'patch[DeviceSetting::ChargeRestart]=pair.restart&thresholdMask' in current
assert 'nativeBatteryVoltageController' in current
assert 'Lower threshold writer retired' not in current
print('Restored shared lower setter, rollback, autosave and restore; new voltage/UI code preserved.')
