"""Offline comparison of saved X5SP4NAG RAM; never loads a hardware library."""
from pathlib import Path
import csv
import hashlib
import json

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
IDENTITY = {0x454: 82, 0x456: 72, 0x740: 26}
FIELDS = (0x490, 0x497, 0x49f, 0x742, 0x7a5, 0x7a6, 0x7b9, 0x7c3,
          0x7cc, 0x7cd, 0x7ce, 0x7d0)
WORDS = (0x30e, 0x434, 0x436, 0x438, 0x4a2, 0x4a4, 0x51c, 0x520, 0x522)
records = []


def add(path, data, valid=None, **metadata):
    if len(data) != 4096:
        return
    if valid is not None and any(not valid[a] for a in IDENTITY):
        return
    if any(data[a] != value for a, value in IDENTITY.items()):
        return
    def byte(address):
        return data[address] if valid is None or valid[address] else None
    def word(address):
        lo, hi = byte(address), byte(address + 1)
        return None if lo is None or hi is None else lo | (hi << 8)
    try:
        name = path.relative_to(ROOT).as_posix()
    except ValueError:
        name = path.as_posix()
    records.append({
        'file': name, 'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'ram_sha256': hashlib.sha256(data).hexdigest(),
        'validity': 'per-byte mask' if valid is not None else 'source record success; no separate mask',
        'fields': {f'0x{a:03X}': byte(a) for a in FIELDS},
        'words_little_endian': {f'0x{a:03X}': word(a) for a in WORDS},
        'bms_rsoc_raw': byte(0x4ab), **metadata,
    })


for root in (ROOT / 'MagicControl/windows/logs/dumps',
             ROOT / 'MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/dumps'):
    for path in sorted(root.glob('*/ec*.bin')):
        if path.name not in ('ec.bin', 'ec-after.bin'):
            continue
        maskpath = path.with_name('ec-valid.bin' if path.name == 'ec.bin' else 'ec-after-valid.bin')
        if maskpath.exists():
            mask = maskpath.read_bytes()
            if len(mask) != 4096:
                continue
            add(path, path.read_bytes(), mask, phase=path.stem)

for path in sorted((ROOT / 'tests/test_util/analysis').rglob('dump.jsonl')):
    if path.stat().st_size > 35_000_000:
        continue
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        try:
            row = json.loads(line)
        except ValueError:
            continue
        values = row.get('bytes')
        if row.get('event') != 'ec_dump' or not isinstance(values, list):
            continue
        data = bytearray(4096)
        valid = bytearray(4096)
        for item in values:
            address, value = item.get('address'), item.get('raw')
            if isinstance(address, int) and 0 <= address < 4096 and isinstance(value, int) and 0 <= value < 256 and not item.get('error'):
                data[address], valid[address] = value, 1
        add(path, data, valid, sample=row.get('sample'), utc=row.get('utc'), phase='ec_dump')

path = Path('C:/Users/admin/Documents/mechrevo/outputs/ec-capture-20261004-120044/ec-table.csv')
if path.exists():
    data, valid = bytearray(4096), bytearray(4096)
    with path.open(encoding='utf-8-sig') as f:
        reader = csv.reader(f, delimiter=';')
        next(reader)
        for row in reader:
            if len(row) < 2 or not row[0].startswith('0x') or not row[1].startswith('0x'):
                continue
            address, value = int(row[0], 16), int(row[1], 16)
            if 0 <= address < 4096 and 0 <= value < 256:
                data[address], valid[address] = value, 1
    add(path, data, valid, phase='historical_csv')

summary = {
    'scope': 'Saved files only; no app, EC, driver, tests or hardware access',
    'snapshot_records': len(records), 'unique_ram_sha256': len({r['ram_sha256'] for r in records}),
    'units_0520_0522_verified_for_wujie': False,
    'initial_and_after_data_are_non_atomic': True, 'records': records,
}
(OUT / 'evidence.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k: v for k, v in summary.items() if k != 'records'}))
