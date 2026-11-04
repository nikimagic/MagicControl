"""Decode saved Wujie EC files only. No driver, app or hardware access."""
from pathlib import Path
from collections import Counter
import csv
import hashlib
import json
import statistics

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[2]
IDENTITY = {0x454: 82, 0x456: 72, 0x740: 26}
records = []

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def add(path, data, valid, **extra):
    if len(data) != 4096 or len(valid) != 4096:
        return
    if any(not valid[a] or data[a] != value for a, value in IDENTITY.items()):
        return
    def word(a):
        return int.from_bytes(data[a:a+2], 'little') if valid[a] and valid[a+1] else None
    words = {hex(a): word(a) for a in [0x402, 0x404, 0x432, 0x434, 0x436, 0x438, 0x4a4, 0x4a6]}
    soc = data[0x4ab] if valid[0x4ab] else None
    full, remaining = words['0x404'], words['0x436']
    ratio = 100 * remaining / full if full and remaining is not None else None
    status, current, voltage = words['0x432'], words['0x434'], words['0x438']
    direction = {0: 'idle', 1: 'discharging', 2: 'charging'}.get(status & 3) if status is not None else None
    power = current * voltage / 1e6 if current is not None and voltage is not None else None
    if direction == 'discharging' and power is not None:
        power = -power
    elif direction is None:
        power = None
    records.append({'file': str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path),
                    'file_sha256': digest(path), 'ram_sha256': hashlib.sha256(data).hexdigest(),
                    'words_le': words, 'bms_rsoc_4ab': soc, 'capacity_ratio_percent': ratio,
                    'soc_minus_ratio': soc - ratio if soc is not None and ratio is not None else None,
                    'state': direction, 'power_from_ec_watts': power,
                    'isdb': data[0x497] & 1 if valid[0x497] else None,
                    'not_atomic': True, **extra})

for tree in ['MagicControl/windows/logs/dumps', 'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/dumps']:
    for path in sorted((ROOT / tree).glob('*/ec*.bin')):
        if path.name not in ['ec.bin', 'ec-after.bin']:
            continue
        mask = path.with_name('ec-valid.bin' if path.name == 'ec.bin' else 'ec-after-valid.bin')
        if mask.exists():
            add(path, path.read_bytes(), mask.read_bytes(), phase=path.stem)

for path in sorted((ROOT / 'tests/test_util/analysis').rglob('dump.jsonl')):
    if path.stat().st_size > 35_000_000:
        continue
    with path.open(encoding='utf-8-sig') as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except ValueError:
                continue
            if row.get('event') != 'ec_dump' or not isinstance(row.get('bytes'), list):
                continue
            data, valid = bytearray(4096), bytearray(4096)
            for item in row['bytes']:
                a, value = item.get('address'), item.get('raw')
                if isinstance(a, int) and 0 <= a < 4096 and isinstance(value, int) and 0 <= value <= 255 and not item.get('error'):
                    data[a], valid[a] = value, 1
            add(path, data, valid, phase='historical ec_dump', sample=row.get('sample'), utc=row.get('utc'))

path = Path('C:/Users/admin/Documents/mechrevo/outputs/ec-capture-20261004-120044/ec-table.csv')
if path.exists():
    data, valid = bytearray(4096), bytearray(4096)
    with path.open(encoding='utf-8-sig') as stream:
        for row in csv.reader(stream, delimiter=';'):
            if len(row) < 2 or not row[0].startswith('0x') or not row[1].startswith('0x'):
                continue
            a, value = int(row[0], 16), int(row[1], 16)
            if 0 <= a < 4096 and 0 <= value <= 255:
                data[a], valid[a] = value, 1
    add(path, data, valid, phase='historical csv')

# Same saved sample contains Windows Rate plus the dashboard's EC-supplemented
# voltage/current. Field-level successful EC provenance is not recorded there:
# a Windows-calculated fallback current can produce agreement by construction.
# Consequently this comparison checks consistency, not independent proof.
comparisons = []
for tree in ['MagicControl/windows/logs/dumps', 'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/dumps']:
    for path in sorted((ROOT / tree).glob('*/sensors.jsonl')):
        errors = []
        frames = 0
        with path.open(encoding='utf-8-sig') as stream:
            for line in stream:
                try:
                    row = json.loads(line)
                except ValueError:
                    continue
                battery = row.get('battery', {})
                if not isinstance(battery, dict) or battery.get('dynamic_stale'):
                    continue
                w, v, i = (battery.get(k) for k in ['watts', 'volts', 'milliamps'])
                if all(isinstance(n, (float, int)) for n in [w, v, i]) and v > 0:
                    frames += 1
                    errors.append(abs(abs(w) - v * abs(i) / 1000))
        if errors:
            comparisons.append({'file': str(path.relative_to(ROOT)), 'samples': frames,
                                'median_absolute_difference_w': statistics.median(errors),
                                'max_absolute_difference_w': max(errors),
                                'independent_ec_success_per_field_unavailable': True})

dsdt = ROOT / 'tests/test_util/analysis/firmware_temperatures/DSDT-0171f11732dc.dat'
fresh = ROOT / 'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/dumps/dump-20261020-160057-0/firmware-before/acpi/registry-0001-DSDT.aml'
ratios = [r for r in records if r['soc_minus_ratio'] is not None]
summary = {'scope': 'Saved file analysis only; no production changes, build, tests, GUI or hardware access',
           'snapshot_count': len(records), 'unique_ram_count': len({r['ram_sha256'] for r in records}),
           'states': dict(Counter(r['state'] for r in records)),
           'soc_comparable_count': len(ratios),
           'max_abs_soc_capacity_ratio_difference_percent': max(abs(r['soc_minus_ratio']) for r in ratios),
           'soc_range': [min(r['bms_rsoc_4ab'] for r in ratios), max(r['bms_rsoc_4ab'] for r in ratios)],
           'dsdt_sha256': digest(dsdt), 'fresh_dsdt_sha256': digest(fresh),
           'dsdt_identical_to_latest_dump': digest(dsdt) == digest(fresh),
           'power_is_calculated_from_ec_current_voltage_not_a_proven_watts_register': True}
result = {'summary': summary, 'snapshots': records, 'saved_dashboard_consistency': comparisons}
(OUT / 'evidence.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(summary, ensure_ascii=False))
print(json.dumps(comparisons, ensure_ascii=False))
