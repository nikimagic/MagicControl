"""Saved files and renderer wiring only; never import a hardware/client module."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2]
EVIDENCE=ROOT/'research/battery-evidence-20261021'
sha=lambda data:hashlib.sha256(data).hexdigest()
manifest=json.loads((EVIDENCE/'manifest.json').read_text(encoding='utf-8'))
for entry in manifest['files']:
 path=(ROOT/entry['path']).resolve()
 assert path.is_relative_to(ROOT),entry['path']
 data=path.read_bytes();assert len(data)==entry['bytes'] and sha(data)==entry['sha256'],entry['path']
index=json.loads((EVIDENCE/'snapshots.json').read_text(encoding='utf-8'))
assert len(index['records'])==36 and not index['missing']
assert index['exact_ec_firmware_obtained'] is False and index['writer_0522_confirmed'] is False
for entry in index['records']:
 data=(EVIDENCE/entry['snapshot']).read_bytes();mask=(EVIDENCE/entry['mask']).read_bytes()
 assert len(data)==len(mask)==4096 and sha(data)==entry['ram_sha256'] and sha(mask)==entry['mask_sha256']
 assert all(mask[a] and data[a]==v for a,v in [(0x454,82),(0x456,72),(0x740,26)])
 for address,value in entry['fields'].items():
  a=int(address,16);assert (data[a]if mask[a]else None)==value
 for address,value in entry['words_little_endian'].items():
  a=int(address,16);assert (data[a]|data[a+1]<<8 if mask[a]and mask[a+1]else None)==value
fixtures=json.loads((ROOT/'tests/offline/fixtures/provenance.json').read_text(encoding='utf-8'))
assert {entry['case']for entry in fixtures}=={'fast','slow'}
for entry in fixtures:
 data=(ROOT/'tests/offline/fixtures'/(entry['case']+'.bin')).read_bytes()
 assert sha(data)==entry['record']['ram_sha256'] and entry['expected']['raw522']==17000
 assert entry['expected']['watts']>25 if entry['case']=='fast' else 0<entry['expected']['watts']<15
# The C++ parity test exercises these adapters; require real renderer consumers
# so a test-only model cannot silently replace actual page wiring.
for platform,call in [('windows','windowsSettingsCards'),('linux','linuxSettingsCards')]:
 page=(ROOT/f'src/gui/{platform}/SettingsPage.cpp').read_text(encoding='utf-8-sig')
 assert call+'(' in page and 'cards.primary' in page and 'cards.diagnostics' in page and 'cards.showDiagnostics' in page
 assert 'ui::primaryBatteryDetails(' not in page and 'ui::batteryDetailColumns(' not in page
geometry=(ROOT/'src/gui/common/Geometry.h').read_text(encoding='utf-8')
assert 'for (const int card : settingsCardOrder)' in geometry
writer=(ROOT/'src/devices/mechrevo/x5sp4nag/windows/BatteryChargeWindows.cpp').read_text(encoding='utf-8')
for call in ['requireBatteryRequest','requireBatteryIdentity','requireBatteryPlatform','requireStoredBatteryThreshold','checkedBatteryWrite','restoreBatteryBits','mergeStoredBatteryWord']:
 assert 'batteryContract::'+call+'(' in writer,call
reader=(ROOT/'src/platform/windows/MonitoringWindows.cpp').read_text(encoding='utf-8')
assert 'uniwill::x5sp4nag::decodeBatteryCore(raw)' in reader
gm6=json.loads((ROOT/'tests/offline/fixtures/gm6-provenance.json').read_text(encoding='utf-8'))
assert {entry['case']for entry in gm6['cases']}=={'gm6-unplugged','gm6-charging','gm6-full'}
for entry in gm6['cases']:
 for filename,record in entry['files'].items():
  data=(ROOT/'tests/offline/fixtures'/filename).read_bytes()
  assert len(data)==4096 and sha(data)==record['sha256'],filename
reader=(ROOT/'src/devices/mechrevo/gm6ir0c/ControlWindows.cpp').read_text(encoding='utf-8')
assert 'result=label(gm6::decodeBatteryCore(raw,batteryModel))' in reader and 'createWindowsAcpiDiagnosticBackend()' in reader
print(f"Evidence: {len(manifest['files'])} original hashes + 6 GM6 fixture hashes; 36 Wujie snapshots, 5 fixtures; actual adapter wiring present; hardware calls=0")
