"""Summarize saved October3 BatteryLimitLab data; no hardware or app launch."""
from pathlib import Path
import hashlib
import json
import statistics

OUT = Path(__file__).resolve().parent
BASE = Path('C:/Users/admin/Documents/mechrevo/BatteryLimitLab')
names = ('20261003-224304-5916', '20261003-091646-reboot-readonly')
result = []
for name in names:
    path = BASE / 'Sessions' / name / 'samples.jsonl'
    groups = {}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        row = json.loads(line)
        b, ec = row.get('Battery', {}), row.get('Bytes', {})
        watts, percent = b.get('RateWatts'), b.get('ChargePercent')
        if not b.get('PowerOnline') or not b.get('Charging'):
            continue
        if not isinstance(watts, (int, float)) or watts <= 1 or not isinstance(percent, (int, float)):
            continue
        key = round(percent), ec.get('1987'), ec.get('2000'), ec.get('1977')
        groups.setdefault(key, []).append((row['At'], watts, b.get('VoltageVolts'), b.get('CurrentAmps')))
    bands = []
    for (percent, platform, lower, upper), rows in sorted(groups.items()):
        bands.append({'rounded_percent': percent, 'platform': platform,
                      'lower_raw': lower, 'upper_raw': upper, 'count': len(rows),
                      'first': rows[0][0], 'last': rows[-1][0],
                      'watts_min_median_max': [min(r[1] for r in rows), statistics.median(r[1] for r in rows), max(r[1] for r in rows)],
                      'volts_min_max': [min(r[2] for r in rows), max(r[2] for r in rows)],
                      'amps_median': statistics.median(r[3] for r in rows)})
    result.append({'session': name, 'source': str(path),
                   'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'bands': bands})
(OUT / 'historical-curve.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('Saved two historical curves; charging-only observations, not causal experiments')
