"""Collect byte evidence from existing EC images. No firmware execution or hardware I/O.

Run with the bundled Python: python -X utf8 collect_percent_evidence.py
New evidence.json is written to an isolated run directory under out.
"""
from pathlib import Path
import hashlib
import json
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
BUILD = ROOT / 'research/gm6-battery-firmware-20261021'
IMAGES = {
    'v111': ('EC-MRO09-candidate.bin', '5059e6cb58addec21ad7fd5693f4c082230fbd8c7692fb07799c90adabae5c4c'),
    'v112': ('EC-MRO10-v1.12.bin', '5659c8217e9d93d3fd6fe278756872d7c5adbd15a0687d35705e2df4bb5c05dd'),
}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def offset(bank, logical):
    return logical if logical < 0x8000 else bank * 0x8000 + logical

def fragment(data, bank, start, end):
    chunk = data[offset(bank, start):offset(bank, start) + end - start]
    return {'bank': bank, 'logical_start': f'{start:04X}', 'logical_end_exclusive': f'{end:04X}',
            'file_start': f'{offset(bank, start):05X}', 'size': len(chunk),
            'sha256': digest(chunk), 'hex': chunk.hex(' ')}

loaded = {}
manifest = {}
for key, (name, expected) in IMAGES.items():
    path = BUILD / name
    data = path.read_bytes()
    if len(data) != 131072 or digest(data) != expected:
        raise ValueError(f'Unexpected input: {path}')
    loaded[key] = data
    manifest[key] = {'path': str(path.relative_to(ROOT)), 'size': len(data), 'sha256': digest(data)}
d = loaded['v112']

# Explicit ranges are byte records, not automatically decoded/reachable code.
ranges = [
    (0, 0x000B, 0x000E), (0, 0x0533, 0x0548), (0, 0x0D3F, 0x0D4B),
    (0, 0x0DD6, 0x0EBB), (0, 0x1570, 0x1576), (0, 0x15A6, 0x15AC),
    (0, 0x1BA6, 0x1BAC), (0, 0x1BD0, 0x1BD6), (0, 0x1C00, 0x1C06),
    (0, 0x1C06, 0x1C0C), (0, 0x1C60, 0x1C66), (0, 0x30DA, 0x30EF),
    (0, 0x8809, 0x8985), (0, 0x8D83, 0x8E7F), (0, 0xA9EE, 0xA9F9),
    (0, 0xAC84, 0xAC8F), (0, 0xC89A, 0xC906),
    (0, 0xC929, 0xC9C9), (0, 0xCF84, 0xCFC6),
    (1, 0xB35F, 0xB3D2), (1, 0xB71D, 0xB726), (1, 0xB756, 0xB777),
    (1, 0xB7AB, 0xB7CF), (1, 0xC050, 0xC05A), (1, 0xC086, 0xC091),
    (1, 0xC14F, 0xC156), (1, 0xC186, 0xC18F), (1, 0xDDA0, 0xDDBC), (1, 0xDE0F, 0xDEB0),
    (2, 0x85F4, 0x860B), (2, 0x8696, 0x86A1), (2, 0xCE5A, 0xCF98),
    (2, 0xC102, 0xC10D), (2, 0xCAD5, 0xCAE3), (2, 0xD827, 0xD82E),
    (2, 0xD875, 0xD87C), (2, 0xD6AD, 0xD6B4), (2, 0xD6CB, 0xD6D2),
]

literal_refs = {}
for address in (0x7B9, 0x7D0, 0x87F, 0x520, 0x522):
    needle = bytes((0x90, address >> 8, address & 255))
    hits = []
    for i in range(len(d) - 2):
        if d[i:i+3] == needle:
            bank = 0 if i < 0x10000 else (1 if i < 0x18000 else 2)
            logical = i if i < 0x10000 else i - bank * 0x8000
            hits.append({'file_offset': f'{i:05X}', 'bank': bank, 'logical': f'{logical:04X}'})
    literal_refs[f'{address:04X}'] = hits

thunks = []
for at in (0x1570, 0x15A6, 0x1BA6, 0x1BD0, 0x1C00, 0x1C06, 0x1C60):
    b = d[at:at+6]
    dispatch = int.from_bytes(b[4:6], 'big')
    thunks.append({'entry': f'{at:04X}', 'bytes': b.hex(' '),
                   'target': f'{int.from_bytes(b[1:3], "big"):04X}',
                   'bank': {0x1100: 0, 0x1114: 1, 0x1128: 2}.get(dispatch),
                   'dispatcher': f'{dispatch:04X}', 'shape_valid': b[0] == 0x90 and b[3] == 2})

# Keil inline switch triples: big-endian destination + one-byte selector.
cases = []
for at in range(0xE32, 0xE4D, 3):
    target = int.from_bytes(d[at:at+2], 'big')
    cases.append({'selector': d[at+2], 'target': f'{target:04X}', 'bytes': d[at:at+3].hex(' ')})

comparison = []
for bank, start, end in [(0, 0x8809, 0x8985), (1, 0xC14F, 0xC156), (1, 0xB37C, 0xB3D2)]:
    a = fragment(loaded['v111'], bank, start, end)
    b = fragment(d, bank, start, end)
    comparison.append({'bank': bank, 'start': f'{start:04X}', 'end_exclusive': f'{end:04X}',
                       'identical': a['hex'] == b['hex'], 'v111_sha256': a['sha256'], 'v112_sha256': b['sha256']})

artifacts = []
for folder in (HERE, HERE.parent / 'ec-handoff-audit'):
    for path in sorted(folder.iterdir()):
        if path.suffix.lower() in ('.c', '.asm', '.py', '.java', '.md'):
            data = path.read_bytes()
            artifacts.append({'path': str(path.relative_to(ROOT)), 'size': len(data), 'sha256': digest(data)})

result = {
    'scope': 'Static bytes only; no firmware execution, hardware access or physical validation',
    'bank_formula': 'logical<0x8000: common; otherwise file=bank*0x8000+logical',
    'images': manifest, 'fragments': [fragment(d, *item) for item in ranges],
    'literal_mov_dptr_refs': literal_refs,
    'literal_search_limit': 'Byte patterns can occur in data and miss computed pointers, aliases and external code',
    'common_thunks': thunks, 'scheduler_switch_cases': cases,
    'scheduler_default_bytes': d[0xE4D:0xE51].hex(' '),
    'cadence': {'percentage_selectors': [4, 14], 'slow_slots_per_cycle': 20,
                'serviced_timer0_ticks_per_slow_slot': 10, 'spacing_serviced_ticks': 100,
                'wall_clock_seconds': None, 'limit': 'Clock/prescaler and delayed/coalesced software ticks not measured'},
    'v111_comparison': comparison, 'source_artifacts': artifacts,
    'primary_interface_reference': {
        'url': 'https://www.sbs-forum.org/specs/sbc110.pdf',
        'sections': '5.1.1 ChargingCurrent (0x14), 5.1.2 ChargingVoltage (0x15)',
        'application_limit': 'Interface units/command meaning, not an exact charger IC identification'},
}
outdir = ROOT / 'out/gm6-battery-firmware-20261021' / ('percent-evidence-' + uuid.uuid4().hex)
outdir.mkdir(parents=True, exist_ok=False)
out = outdir / 'evidence.json'
out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(f'Static evidence written: {out}; {len(ranges)} byte ranges')
