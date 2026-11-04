"""Summarize saved read-only traces. Does not open hardware or launch software."""
from pathlib import Path
import hashlib
import json

HERE = Path(__file__).resolve().parent
paths = [HERE / 'ec-idle.jsonl', *(HERE / name / 'ec-idle.jsonl' for name in ['observe', 'initial', 'levels'])]
rows = []
sources = []
for path in paths:
    if not path.exists():
        continue
    data = path.read_bytes()
    samples = [json.loads(line) for line in data.decode('utf-8-sig').splitlines() if line.strip()]
    assert all(sample['ec_write_calls'] == 0 for sample in samples)
    sources.append({'path': str(path.relative_to(HERE)), 'samples': len(samples), 'sha256': hashlib.sha256(data).hexdigest()})
    rows.extend(samples)
rows.sort(key=lambda row: row['utc'])
power_events, blank_events, working_levels = [], [], []
previous = None
previous_work = None
for row in rows:
    if previous:
        if previous['raw']['0x7CC'] != row['raw']['0x7CC']:
            power_events.append({'utc': row['utc'], 'before_7CC': previous['raw']['0x7CC'],
                                 'after_7CC': row['raw']['0x7CC'], 'counter': row['counter'],
                                 'platform': row['raw']['0x7C3'], 'rom': row['raw']['0x770']})
        if previous['idle_blank'] != row['idle_blank']:
            blank_events.append({'before': previous, 'after': row})
    if '0x826' in row['raw']:
        state = [row['raw'][key] for key in ['0x825', '0x826', '0x827']]
        if state != previous_work:
            working_levels.append({'utc': row['utc'], 'AC_current_DC': state,
                                   'raw_78C': row['raw']['0x78C'], 'windows_power': row.get('windows_power')})
        previous_work = state
    previous = row
summary = {'samples': len(rows), 'sources': sources,
           'platform_values': sorted(set(row['raw']['0x7C3'] for row in rows)),
           'rom_values': sorted(set(row['raw']['0x770'] for row in rows)),
           'max_counter': max(row['counter'] for row in rows),
           'all_samples_ec_write_calls_zero': True,
           'power_events': power_events, 'idle_blank_events': blank_events,
           'working_level_events': working_levels, 'last': rows[-1]}
(HERE / 'live-summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'samples': summary['samples'], 'blank_events': len(blank_events),
                  'max_counter': summary['max_counter'], 'last_counter': rows[-1]['counter'],
                  'last_utc': rows[-1]['utc'], 'power_events': len(power_events),
                  'working_level_events': working_levels}))
