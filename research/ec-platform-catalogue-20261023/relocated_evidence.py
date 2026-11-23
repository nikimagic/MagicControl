"""Resolve reviewed historical firmware paths and verify bytes by SHA-256."""
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path,PureWindowsPath
import uuid

ROOT=Path(__file__).resolve().parents[2]
EVIDENCE=ROOT/'research/m124'
OLD_M124=ROOT/'build/m124'
OLD_ECP23=ROOT/'build/ecp23'
ECP23=ROOT/'research/ecp23'
OLD_B23=ROOT/'build/b23'
B23=ROOT/'research/b23'
OLD_UB23=ROOT/'build/ub23'
UB23=ROOT/'research/ub23'
OLD_G9_DUMP=ROOT/'build/g9.bin.dump'
G9_DUMP=ROOT/'research/g9.bin.dump'
OLD_G9=ROOT/'build/g9.bin'
MOVED_G9=ROOT/'research/g9.bin'
G9=ROOT/'research/gm6-battery-firmware-20261021/BIOS-MRO09-from-UCP.bin'
OLD_HONOR=ROOT/'build/honor-analysis'
HONOR=ROOT.parent/'Decompile/HONOR_HLYL-WXX9/honor-analysis'
OLD_HONOR_OEM=ROOT/'build/honor-oem-analysis'
HONOR_OEM=ROOT.parent/'Decompile/HONOR_HLYL-WXX9/honor-oem-analysis'
OLD_HYDROC_FOLLOWUP=ROOT/'build/hydroc-port-followup'
HYDROC_FOLLOWUP=ROOT/'research/build-evidence-20261107/HYDROC/hydroc-port-followup'
OLD_BATTERY_TEMPERATURE=ROOT/'build/battery-temperature-20261020'
BATTERY_TEMPERATURE=ROOT.parent/'Reference_Projects/Battery_Temperature_20261020'
OLD_NESTED_OEM=ROOT/'build/nested-oem-references'
NESTED_OEM=ROOT.parent/'Decompile/NESTED_OEM_7578'
OLD_ARCHIVE_PYDEPS=ROOT/'build/research-archive-reader-pydeps'
ARCHIVE_PYDEPS=ROOT.parent/'Reference_Projects/Research_Archive_Python_Dependencies'
OLD_CHARGING_HOLD=ROOT/'build/charging-hold-20261023'
CHARGING_HOLD=ROOT.parent/'Decompile/NUCTOOL_Charge_20261023'
CHARGING_HOLD_LOGS=ROOT/'research/charging-hold-followup-20261023/portable-log-snapshots'
WUJIE_SAVED_LOG_DIRS=('wujie-charge-analysis-20261021-1147',
                      'wujie-charge-analysis-20261021-1220',
                      'wujie-discharge5-20261021')
OLD_GM6_HEALTHY_ACPI=ROOT/'build/gm6-healthy-acpi'
GM6_HEALTHY_ACPI=ROOT.parent/'Decompile/GM6IR0C/GM6_ACPI_Healthy_20261021'
OLD_GM6_NVAR=ROOT/'build/gm6-nvar-comparison'
GM6_NVAR=ROOT.parent/'Decompile/GM6IR0C/GM6_NVAR_Comparison_20261024'
OLD_GM6_BIOS_DSDT=ROOT/'build/gm6-bios-dsdt'
GM6_BIOS_DSDT=ROOT.parent/'Decompile/GM6IR0C/BIOS_DSDT'
OLD_GM6_RESEARCH=ROOT/'build/research-gm6ir0c'
GM6_RESEARCH=ROOT/'research/research-gm6ir0c'
OLD_GM6_CC=OLD_GM6_RESEARCH/'all-active'
GM6_CC=ROOT.parent/'Decompile/GM6IR0C/Control_Center'
OLD_GM6_EARLY=ROOT/'build/gm6ir0c'
GM6_EARLY=ROOT.parent/'Decompile/GM6IR0C/Ранние_разборы'
OLD_HYDROC_RESEARCH=ROOT/'build/research-hydroc16g2'
HYDROC_RESEARCH=ROOT/'research/research-hydroc16g2'
OLD_HINGE_RESEARCH=ROOT/'build/research-hinge-20261020'
HINGE_RESEARCH=ROOT/'research/build-evidence-20261107/audit-batch/OTHER/research-hinge-20261020'
OLD_THERMAL_RESEARCH=ROOT/'build/thermal-research'
THERMAL_RESEARCH=ROOT/'research/build-evidence-20261107/OTHER/thermal-research'
OLD_HONOR_DEEP=ROOT/'build/honor-deep-0.1.58'
HONOR_DEEP=ROOT.parent/'Decompile/HONOR_HLYL-WXX9/honor-deep-0.1.58'
OLD_INSTRUMENT_DRAFT=ROOT/'build/instrument-330-draft'
INSTRUMENT_DRAFT=ROOT/'research/lenovo-instrument-330-draft-20261103'
OLD_BACKLIGHT_DRIVER=ROOT/'build/research-backlight-driver-20261021'
BACKLIGHT_DRIVER=ROOT/'research/backlight-driver-20261021'
OLD_REVIEW_HARNESS=ROOT/'build/review-harness'
REVIEW_HARNESS=ROOT/'research/review-harness-20261013'
LHM_REFERENCES=ROOT.parent/'Reference_Projects/LibreHardwareMonitor_Source_References'
LHM_FILES={name.casefold():name for name in ('research-SpdThermalSensor.cs','research-MemoryGroup.cs','research-intel-cpu.cs','research-DimmMemory.cs')}
HONOR_TRANSFERS=ROOT.parent/'Reference_Projects/Honor_Project_Transfers_20261017'
HONOR_TRANSFER_FILES={name.casefold():name for name in ('Honor-project-transfer-20261017.zip','Honor-project-transfer-20261017-v2.zip')}
OLD_OWNER_DELIVERY=ROOT/'build/owner-delivery'
OWNER_DELIVERY=ROOT/'research/build-evidence-20261107/release-archive/PRODUCT/owner-delivery'
OWNER_SYS_FILES={name.casefold():name for name in ('MagicControl-0.1.98-windows-x64-portable','MagicControl-0.1.102-windows-x64-portable','MagicControl-0.1.101-windows-x64-portable')}
OLD_CAPTURE_DSDT=ROOT/'tests/test_util/analysis/EC_20261012_073054/acpi/DSDT.dat'
CAPTURE_DSDT=ROOT/'out/test_util/analysis/EC_20261012_073054/acpi/DSDT.dat'
OLD_IMAGES=ROOT/'build/gm6-battery-firmware-20261021/mro10-static'
OLD_GM6_BATTERY=ROOT/'build/gm6-battery-firmware-20261021'
GM6_BATTERY=ROOT/'research/gm6-battery-firmware-20261021'
GM6_BATTERY_ARCHIVE=ROOT.parent/'Decompile/GM6IR0C/BIOS_и_EC'
IMAGE_COPIES={'backup.fd':'b0.bin','bios.bin':'b1.bin','new.fd':'b2.bin'}
ECP23_IDENTICAL_DUMPS=(
 (ECP23/'171a0d/b0.bin.dump',ECP23/'171a0d/b1.bin.dump'),
 (ECP23/'4fc8eb/b0.bin.dump',ECP23/'4fc8eb/b1.bin.dump'),
 # These four old folders were already absent before relocation. The retained
 # u0 bytes were verified against the earlier per-file comparison ledger.
 (ECP23/'171a0d/u.fv.dump',ECP23/'171a0d/u0.fv.dump'),
 (ECP23/'171a0d/u1.fv.dump',ECP23/'171a0d/u0.fv.dump'),
 (ECP23/'171a0d/u2.fv.dump',ECP23/'171a0d/u0.fv.dump'),
 (ECP23/'4fc8eb/u.fv.dump',ECP23/'4fc8eb/u0.fv.dump'),
)
HISTORICAL_WINDOWS_ROOT=PureWindowsPath(r'C:\Users\admin\Documents\mechrevo_dsh\MagicControl')
KNOWN_RELATIVE_ROOTS={('build','m124'),('build','ecp23'),('build','b23'),('build','ub23'),('build','g9.bin'),('build','g9.bin.dump'),('build','honor-analysis'),('build','honor-oem-analysis'),('build','hydroc-port-followup'),('build','battery-temperature-20261020'),('build','nested-oem-references'),('build','research-archive-reader-pydeps'),('build','charging-hold-20261023'),('build','gm6-healthy-acpi'),('build','gm6-nvar-comparison'),('build','gm6-bios-dsdt'),('build','research-gm6ir0c'),
 ('build','gm6-battery-firmware-20261021'),('research','m124'),('research','ecp23'),('research','b23'),('research','ub23'),('research','g9.bin'),('research','g9.bin.dump')}

GM6_ASM=ROOT/'research/build-evidence-20261107/gm6-firmware-asm'
GM6_ASM_SHA256={'gm6-bios-acpi-smm.asm': '751d8e7065c45b0428c9e87722b566883c3c632e7582f23d714a76a78a5ef1b0', 'gm6-healthy-driver-original.asm': 'e2442d5526a939a079e02c6b87ae20871e8c947ccfd13f0a0e692a86fbb36488', 'gm6-power-other-consumers.asm': '2302edb9a45709405340c6011e977f56711543912dedfa87238ca8bfa9fd46fb', 'gm6-rom22-bank0.asm': 'cf653dba8ff67db88709f2c33dd1d0fa8334c9eba48c740ffaf30efe9414a9f8', 'gm6-rom22-bank2.asm': '23164dc0dab80b9b22ed05d1128d4b31f7b1e300a1bb10458110a706ba71669c'}
KNOWN_RELATIVE_ROOTS.update({('build',name) for name in GM6_ASM_SHA256})
KNOWN_RELATIVE_ROOTS.update({('build',name) for name in WUJIE_SAVED_LOG_DIRS})
KNOWN_RELATIVE_ROOTS.update({('build',name) for name in LHM_FILES})
KNOWN_RELATIVE_ROOTS.update({('build',name) for name in HONOR_TRANSFER_FILES})
KNOWN_RELATIVE_ROOTS.add(('build','owner-delivery'))
KNOWN_RELATIVE_ROOTS.add(('tests','test_util'))
KNOWN_RELATIVE_ROOTS.update({('build',name) for name in ('gm6ir0c','research-hydroc16g2','research-hinge-20261020','thermal-research','honor-deep-0.1.58')})
KNOWN_RELATIVE_ROOTS.update({('build',name) for name in ('instrument-330-draft','research-backlight-driver-20261021','review-harness')})


def windows_recorded_parts(recorded):
 path=PureWindowsPath(str(recorded))
 if path.is_absolute():
  try:path=path.relative_to(HISTORICAL_WINDOWS_ROOT)
  except ValueError as exc:raise ValueError('Unmapped external Windows evidence path') from exc
 elif path.drive or path.root:raise ValueError('Ambiguous Windows evidence path')
 if '..' in path.parts:raise ValueError('Invalid Windows evidence path')
 return path.parts

def input_path(recorded,*,platform_name=None):
 text=os.fspath(recorded)
 platform_name=os.name if platform_name is None else platform_name
 if platform_name=='nt':return Path(text)
 if platform_name!='posix':raise ValueError('Unsupported evidence path platform')
 windows=PureWindowsPath(text)
 if windows.drive or '\\' in text:
  parts=windows_recorded_parts(text)
  prefix=tuple(p.casefold() for p in parts[:2])
  if prefix in KNOWN_RELATIVE_ROOTS:parts=(*prefix,*parts[2:])
  elif not windows.is_absolute():raise ValueError('Unmapped relative Windows evidence path')
  return ROOT.joinpath(*parts) if windows.is_absolute() else Path(*parts)
 return Path(text)

def resolve_recorded(recorded,*,platform_name=None):
 path=input_path(recorded,platform_name=platform_name)
 if not path.is_absolute():
  parts=tuple(p.casefold() for p in path.parts)
  if len(parts)>=2 and parts[:2] in KNOWN_RELATIVE_ROOTS:
   if path.drive or '..' in path.parts:raise ValueError('Invalid relative historical evidence path')
   path=ROOT/path
 if path in (OLD_G9,MOVED_G9):return G9
 if path==OLD_CAPTURE_DSDT:return CAPTURE_DSDT
 if path.parent==ROOT/'build' and path.name in GM6_ASM_SHA256:return GM6_ASM/path.name
 if path.parent==ROOT/'build' and path.name.casefold() in LHM_FILES:return LHM_REFERENCES/LHM_FILES[path.name.casefold()]
 if path.parent==ROOT/'build' and path.name.casefold() in HONOR_TRANSFER_FILES:return HONOR_TRANSFERS/HONOR_TRANSFER_FILES[path.name.casefold()]
 try:relative=path.relative_to(OLD_OWNER_DELIVERY)
 except ValueError:pass
 else:
  if '..' in relative.parts:raise ValueError('Invalid owner-delivery evidence path')
  parts=relative.parts
  if len(parts)==4 and parts[0].casefold() in OWNER_SYS_FILES and tuple(p.casefold() for p in parts[1:])==('drivers','uniwill','uwacpidriver.sys'):
   return OWNER_DELIVERY/OWNER_SYS_FILES[parts[0].casefold()]/'drivers/Uniwill/UWACPIDriver.sys'
 try:relative=path.relative_to(OLD_CHARGING_HOLD)
 except ValueError:pass
 else:
  if '..' in relative.parts:raise ValueError('Invalid charging-hold evidence path')
  if relative.parts and relative.parts[0].casefold()=='portable-log-snapshots':
   return CHARGING_HOLD_LOGS.joinpath(*relative.parts[1:])
  return CHARGING_HOLD/relative
 for name in WUJIE_SAVED_LOG_DIRS:
  try:relative=path.relative_to(ROOT/'build'/name)
  except ValueError:continue
  if '..' in relative.parts:raise ValueError('Invalid Wujie log evidence path')
  return ROOT/'research'/name/relative
 for old,new in ((OLD_M124,EVIDENCE),(OLD_ECP23,ECP23),(OLD_B23,B23),(OLD_UB23,UB23),(OLD_G9_DUMP,G9_DUMP),(OLD_HONOR,HONOR),(OLD_HONOR_OEM,HONOR_OEM),(OLD_HONOR_DEEP,HONOR_DEEP),(OLD_HYDROC_FOLLOWUP,HYDROC_FOLLOWUP),(OLD_BATTERY_TEMPERATURE,BATTERY_TEMPERATURE),(OLD_NESTED_OEM,NESTED_OEM),(OLD_ARCHIVE_PYDEPS,ARCHIVE_PYDEPS),(OLD_GM6_HEALTHY_ACPI,GM6_HEALTHY_ACPI),(OLD_GM6_NVAR,GM6_NVAR),(OLD_GM6_BIOS_DSDT,GM6_BIOS_DSDT),(OLD_GM6_CC,GM6_CC),(OLD_GM6_RESEARCH,GM6_RESEARCH),(OLD_GM6_EARLY,GM6_EARLY),(OLD_HYDROC_RESEARCH,HYDROC_RESEARCH),(OLD_HINGE_RESEARCH,HINGE_RESEARCH),(OLD_THERMAL_RESEARCH,THERMAL_RESEARCH),(OLD_INSTRUMENT_DRAFT,INSTRUMENT_DRAFT),(OLD_BACKLIGHT_DRIVER,BACKLIGHT_DRIVER),(OLD_REVIEW_HARNESS,REVIEW_HARNESS)):
  try:relative=path.relative_to(old)
  except ValueError:continue
  if '..' in relative.parts:raise ValueError('Invalid historical evidence path')
  path=new/relative
  break
 for old,new in ECP23_IDENTICAL_DUMPS:
  try:relative=path.relative_to(old)
  except ValueError:continue
  if '..' in relative.parts:raise ValueError('Invalid historical duplicate path')
  return new/relative
 for original,copy in IMAGE_COPIES.items():
  if path==OLD_IMAGES/original:return EVIDENCE/copy
 try:relative=path.relative_to(OLD_GM6_BATTERY)
 except ValueError:pass
 else:
  if '..' in relative.parts:raise ValueError('Invalid historical GM6 battery evidence path')
  preserved=GM6_BATTERY/relative
  return preserved if preserved.exists() else GM6_BATTERY_ARCHIVE/relative
 return path

def read_verified(recorded,expected_sha256):
 path=resolve_recorded(recorded);data=path.read_bytes()
 if hashlib.sha256(data).hexdigest()!=expected_sha256:
  raise ValueError('Historical evidence SHA mismatch: '+str(path))
 return data,path

def new_run_dir(label):
 if not label or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in label):
  raise ValueError('Invalid output label')
 run=ROOT/'out/ec-platform-catalogue-20261023'/(label+'-'+datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex)
 run.mkdir(parents=True,exist_ok=False)
 return run
