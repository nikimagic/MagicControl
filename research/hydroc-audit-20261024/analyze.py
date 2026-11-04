"""Offline HYDROC source/archive inventory. Does not load OEM or hardware modules."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
sha = lambda data: hashlib.sha256(data).hexdigest()
archive = ROOT / 'Users_logs/logs_HYDROC-16.zip'
with zipfile.ZipFile(archive) as package:
    names = package.namelist()
    def member(suffix):
        found = [n for n in names if n.endswith('/' + suffix)]
        if len(found) != 1:
            raise ValueError((suffix, found))
        return found[0], package.read(found[0])
    ram_name, ram = member('ec.bin')
    valid_name, valid = member('ec-valid.bin')
    if len(ram) != 4096 or len(valid) != 4096 or valid.count(1) != 4096:
        raise ValueError('Incomplete RAM evidence')
    _, inventory = member('inventory.json')
    fields = [0x740,0x74C,0x78E,0x7C8,0x7C7,0x73C,0x766,0x7A6,0x7B9,
              0x7D0,0x7C3,0x770,0x7CC,0x7CD,0x7CE,0x726,0x727,0x751,0x7C5,0x7C6]
    snapshot = {f'0x{a:03X}': {'raw': ram[a], 'hex': f'{ram[a]:02X}'} for a in fields}
    probes = {}
    for suffix in ('fans-default-state.json','fans-ec-max.json','fans-nvml-max.json'):
        name, data = member(suffix)
        probes[suffix] = json.loads(data.decode('utf-8-sig'))
    members = {n: {'bytes': len(data), 'sha256': sha(data)}
               for n in names if not n.endswith('/') for data in (package.read(n),)}

source_names = [
    'src/devices/eluktronics/hydroc-16-g2/Profile.cpp',
    'src/devices/eluktronics/hydroc-16-g2/DiagnosticsWindows.cpp',
    'src/protocols/uniwill/hydroc16g2/Diagnostics.h',
    'src/protocols/uniwill/hydroc16g2/Battery.h',
    'src/protocols/uniwill/hydroc16g2/KeyboardHid.h',
    'src/core/include/MagicControl/DeviceProfile.h',
    'src/platform/windows/NativeBatteryWindows.cpp',
    'src/platform/windows/BatteryEnforcementWindows.h',
    'src/platform/windows/BatteryEnforcementWindows.cpp',
    'src/platform/windows/uniwill-acpi/BackendWindows.cpp',
    'src/platform/windows/DiagnosticDumpWindows.cpp',
    'src/platform/common/DiagnosticDump.cpp',
    'src/platform/windows/RamFanFactoryWindows.cpp',
    'src/protocols/uniwill/RamFanFactory.cpp',
    'src/platform/windows/pawnio/IntelTelemetryWindows.cpp',
    'src/platform/windows/pawnio/IntelPchWindows.cpp',
    'src/platform/windows/pawnio/SensorModuleWindows.h',
    'src/platform/linux/BackendLinux.cpp',
]
oem_root = ROOT / 'research/devices/hydroc-16-g2/oem-5.23.51.56/recovered-20261019/readable-csharp'
oem_prefixes = ('020001D1','020002D6','02000427','0200044A','02000456',
                '020002A2','020000D7','020000DD','020000DF','020000DB','02000217','02000257')
oem_files = [p for p in oem_root.glob('*.cs') if p.name.startswith(oem_prefixes)]
sources = {}
for name in source_names:
    p = ROOT / name
    sources[name] = {'bytes': p.stat().st_size, 'sha256': sha(p.read_bytes())}
for p in oem_files:
    sources[p.relative_to(ROOT).as_posix()] = {'bytes': p.stat().st_size, 'sha256': sha(p.read_bytes())}
knowledge = [
    'research/ec-platform-catalogue-20261023/PLATFORMS-5-6-7.md',
    'research/ec-platform-catalogue-20261023/CORPUS-STATUS.md',
    'research/ec-platform-catalogue-20261023/PHASE154-TEN-EC-COUNTER-POLICIES.md',
    'research/ec-platform-catalogue-20261023/PHASE155-TEN-EC-COUNTER-ENTRY-GUARDS.md',
    'research/ec-platform-catalogue-20261023/PHASE157-GM6-P12-CAPS-AND-RGB-SELECTOR.md',
    'research/ec-platform-catalogue-20261023/PHASE161-GM6-P2-DR-P13-CALLERS.md',
    'research/ec-platform-catalogue-20261023/PHASE162-FNLOCK-PLATFORM-ROM-PRIORITY.md',
    'research/ec-platform-catalogue-20261023/PHASE163-LAST-DIRECT-ANCHORS.md',
    'research/analysis-20261020/keyboard-backlight-agents/hydroc.txt',
]
for name in knowledge:
    p = ROOT / name
    data = p.read_bytes()
    sources[name] = {'bytes': len(data), 'sha256': sha(data)}
    if p.name in ('CORPUS-STATUS.md', 'PLATFORMS-5-6-7.md'):
        # These aggregate indexes are still being updated in other work.
        # Preserve the exact input read here instead of chasing a moving hash.
        snapshots = OUT / 'knowledge-index-snapshots'
        snapshots.mkdir(exist_ok=True)
        captured = snapshots / p.name
        captured.write_bytes(data)
        sources[name]['snapshot'] = captured.relative_to(ROOT).as_posix()

needles = ['SetHealthProtectionStatus','SetHealthProtectionHigh','SetHealthProtectionMiddle',
           'SetHealthProtectionLow','SetPL1Value','SetPL2Value','SetPL4Value',
           'IsSupportCPUPL4DoubleFlag','RefreshDefaultFanTableAll','RefreshDefaultFanTable(',
           'GetEcCpuFanRpm','GetEcGpuFanRpm','SetECSupport','LM_Init','Set_Welcome_TimeOut']
anchors = []
for p in oem_files:
    for line_number, line in enumerate(p.read_text(encoding='utf-8-sig').splitlines(), 1):
        matches = [word for word in needles if word in line]
        if matches:
            anchors.append({'file': p.relative_to(ROOT).as_posix(), 'line': line_number,
                            'terms': matches, 'scope': 'Text reference/declaration; not a full caller graph'})

result = {
    'date': '2026-10-24', 'revision': subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip(),
    'scope': 'Existing local files only; no OEM execution or hardware access',
    'archive': {'path': archive.relative_to(ROOT).as_posix(), 'sha256': sha(archive.read_bytes()), 'members': members},
    'original_log_version': '0.1.69', 'dump_scope': '2026-10-19T15:13:30Z; approximately 7 seconds; not current live state',
    'ram': {'member': ram_name,'sha256': sha(ram),'bytes': len(ram),'valid_ones':valid.count(1),'registers':snapshot},
    'derived_from_exact_oem_format': {
        'cpu_rpm_candidate': (ram[0x464]<<8)|ram[0x465],
        'gpu_rpm_candidate': (ram[0x46C]<<8)|ram[0x46B],
        'cpu_duty_candidate_percent': ram[0x75B]/2,
        'gpu_duty_candidate_percent': ram[0x75C]/2,
        'cycles_word_4A6': int.from_bytes(ram[0x4A6:0x4A8],'little'),
        'temperature_word_4A2_raw': int.from_bytes(ram[0x4A2:0x4A4],'little'),
        'temperature_if_tenths_kelvin': round(int.from_bytes(ram[0x4A2:0x4A4],'little')/10-273.15,2),
        'native_policy_bits_7A6':ram[0x7A6]&0x30,
        'factory_guard_LC_connected_bit': bool(ram[0x7C7]&2),
        'LC_capability_bit':bool(ram[0x7C8]&0x20),
        'PL4_double_flag':bool(ram[0x727]&0x80),
        'physical_calibration_verified':False},
    'old_probe_results':probes,
    'old_inventory_has_feature_report_metadata':bool(re.search(b'FeatureReportByteLength|feature_report_bytes|featureBytes',inventory)),
    'firmware_scope':'Exact executable ELU11 EC is not established by the selected 23-unit corpus; GM6 EC1.12 is not HYDROC EC1.12',
    'sources':sources, 'oem_text_anchors':anchors,
    'hardware_access':False,'new_tests_run':False,'source_changes_to_HYDROC_controls':False,
}
(OUT/'EVIDENCE.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'source_files':len(sources),'oem_anchors':len(anchors),
    'platform':ram[0x7C3],'ROM':ram[0x770],'LC_connected_bit':bool(ram[0x7C7]&2),
    'PL4_double_flag':bool(ram[0x727]&0x80),'rpm_candidates':[(ram[0x464]<<8)|ram[0x465],(ram[0x46C]<<8)|ram[0x46B]]},indent=2))
