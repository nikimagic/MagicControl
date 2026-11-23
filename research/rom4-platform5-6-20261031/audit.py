"""ROM4 cross-image saved-file audit. No application, OEM or hardware execution."""
from pathlib import Path
import sys as _resolver_sys
_resolver_sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'ec-platform-catalogue-20261023'))
from relocated_evidence import read_verified
import hashlib
import json
import sys

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[1]
CAT = ROOT / 'research/ec-platform-catalogue-20261023'
sys.path.insert(0, str(ROOT / '../Decompile/HONOR_HLYL-WXX9/honor-analysis'));sys.dont_write_bytecode=True
from disasm51 import addresses, codeanalyzer, instructions

def load(name):
    return json.loads((CAT / name).read_text(encoding='utf-8-sig'))

units = {u['sha256']: u for u in load('source-units.json') + load('additional-source-units.json')}
blobs = {}
for digest, unit in units.items():
    data, _ = read_verified(unit['path'], unit['sha256'])
    if hashlib.sha256(data).hexdigest() != digest:
        raise RuntimeError('Different source: ' + unit['source_label'])
    blobs[digest] = data
consumers = load('reviewed-consumer-behavior-index.json')['rows']
for row in consumers:
    anchor = bytes.fromhex(row['anchor_hex'])
    pos = row['physical']
    if blobs[row['sha256']][pos:pos+len(anchor)] != anchor:
        raise RuntimeError('Consumer byte mismatch: ' + row['id'])
bios = load('reviewed-bios-consumer-index.json')['rows']
for row in bios:
    read_verified(row['module_file'], row['module_sha256'])
oem = load('oem-source-scope-audit.json')
for source in oem['sources']:
    file = Path(source['file'])
    if not file.is_absolute() and not file.is_file():
        file = CAT / file
    if hashlib.sha256(file.read_bytes()).hexdigest() != source['sha256']:
        raise RuntimeError('OEM audit reference mismatch: ' + str(file))

# The older scope's protected-L16 note is stale: a full analysis copy exists.
# Verify that copy and the concrete source types; this is not an EC flash image.
l16 = ROOT / 'research/l16-complete-decompile-20261030'
l16_digest = hashlib.sha256((l16/'GCUService-recovered-analysis.bin').read_bytes()).hexdigest()
if l16_digest != 'de44d0599c728acf44220e4d07bc37b4f3078a0c87e1315e8ee83701a6db66f8':
    raise RuntimeError('Different recovered L16 assembly')
l16_types = ['0200005D-Define.ECSpec.cs', '020000FD-MyECIO.MyEcCtrl.cs',
             '020001DD-GCUService.MySystem.BatteryProtection2.cs',
             '0200043E-MyControlCenter.MyFan.MyFanManager_RamFan1p5.cs']
l16_sources = [{'file': name, 'sha256': hashlib.sha256((l16/'raw-csharp'/name).read_bytes()).hexdigest()}
               for name in l16_types]

matrix, seen = [], set()
for row in load('platform-state-truth.json')['gate_rows']:
    if row['source'] in seen:
        continue
    seen.add(row['source'])
    source = row['source']
    fallback = ('conditional raw87F fallback' if source.startswith('GMxHG') else
                'conditional898 shadow route; negative marker then clearsReached/returns' if source.startswith('XxRP') else
                'negative marker still continues upper body' if source.startswith('X6DR') else
                'negative marker clearsReached/returns')
    matrix.append({'source': source, 'sha256': row['sha256'], 'bank': row['bank'], 'CODE': row['CODE'],
                   'primary_positive_route': row['expression'],
                   'P5_FF_primary_route': 'platform==5' in row['expression'],
                   'P6_FF_primary_route': False,
                   'P5_ROM4_primary_route': True, 'P6_ROM4_primary_route': True,
                   'marker_false_behavior': fallback,
                   'scope': 'Static selected task; primary marker is not complete cutoff/physical-current proof'})
for row in load('gx-native-charge-paths.json')['rows']:
    first = row['sha256'].startswith('34c')
    matrix.append({'source': row['source'], 'sha256': row['sha256'], 'selected_gate': row['selected_gate'],
                   'P5_FF_route': True if first else False,
                   'P6_FF_route': 'conditional87F.low7 in1..100' if first else False,
                   'P5_ROM4_route': True, 'P6_ROM4_route': True,
                   'scope': row['scope']})

wujie_file = ROOT / 'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin'
wujie = wujie_file.read_bytes()
wujie_sha = hashlib.sha256(wujie).hexdigest()
if wujie_sha != '5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8':
    raise RuntimeError('Different Wujie payload')
matrix.append({'source': 'Wujie X5SP4NAG embedded1.18', 'sha256': wujie_sha,
               'primary_positive_route': 'P4 OR R4', 'P5_FF_route': False, 'P6_FF_route': False,
               'P5_ROM4_route': True, 'P6_ROM4_route': True,
               'scope': 'Embedded image; running EC flash not independently attested; P5/P6 not device-tested'})

rom4 = [r for r in consumers if any(a.get('value') == 4 and 'R0' in a.get('namespace', '') for a in r.get('associations', []))]
windows = []
def window(name, data, bank, start, end):
    rom = data[:0x8000] + data[(bank+1)*0x8000:(bank+2)*0x8000]
    dec = codeanalyzer.CodeAnalyzer(instructions.Instructions(), rom, addresses.Addresses(), 0, True, [], set())
    pc, code = start, []
    while pc < end:
        decoded = dec._CodeAnalyzer__disassemble_instruction(pc)
        if decoded is None:
            raise RuntimeError(f'Undecoded {name}/{pc:04X}')
        text, length = decoded
        code.append({'CODE': pc, 'hex': rom[pc:pc+length].hex(), 'text': text.strip()})
        pc += length
    if pc != end:
        raise RuntimeError('Boundary mismatch ' + name)
    windows.append({'name': name, 'bank': bank, 'start': start, 'end': end, 'instructions': code})
for sha, unit in units.items():
    if unit['kind'] == 'EC' and unit['source_label'].startswith('EC-MRO'):
        window(unit['source_label'] + '/CPU target S4 floor', blobs[sha], 2, 0x8fa9, 0x8fc6)
window('Wujie factory fan selector', wujie, 2, 0x87ab, 0x87dd)
window('Wujie default factory fan header', wujie, 2, 0x88c3, 0x88d5)
window('Wujie conditional power-budget selector', wujie, 2, 0xb01c, 0xb03d)
window('Wujie SKU18 power-budget override', wujie, 2, 0xb084, 0xb0a1)
window('Wujie charge admission', wujie, 2, 0xcffd, 0xd025)

result = {'scope': 'Saved-file audit only; no EC access/writes, OEM/firmware/app execution or product build',
          'source_components_verified': len(units), 'catalogue_EC': sum(u['kind']=='EC' for u in units.values()),
          'catalogue_BIOS': sum(u['kind']=='BIOS' for u in units.values()),
          'consumer_anchor_bytes_verified': len(consumers), 'BIOS_modules_verified': len(bios),
          'OEM_audit_reference_hashes_verified': len(oem['sources']),
          'OEM_scope': oem['summary'], 'OEM_original_limits': oem['remaining'],
          'L16_supplement': {'recovered_analysis_sha256': l16_digest,
                             'exported_top_level_types': len(list((l16/'raw-csharp').glob('*.cs'))),
                             'selected_sources': l16_sources,
                             'ROM_getter_addresses': ['771.bit0', '772.byte'],
                             'scope': 'Recovered OEM code checked; these getter names do not mean ROM0=770. Not an exact L16 EC1.16 firmware image.'},
          'units': list(units.values()), 'charge_matrix': matrix,
          'ROM4_consumer_count': len(rom4),
          'ROM4_consumers': [{k:r[k] for k in ['id','source','kind','bank','CODE','condition','effect','report','scope']} for r in rom4],
          'selected_windows': windows,
          'limits': ['Not all indirect execution paths/physical effects are proven by the catalogue.',
                     'Exact L16 EC1.16 and GM5 EC1.15 are not authenticated by similarly named family payloads.',
                     'Honor and HPBSD have separate device protocols; no generic Uniwill ROM4 mapping.',
                     'P5/P6 ROM4 was not device-tested. Raw parameters are not measured watts/PWM/RPM.']}
(OUT/'evidence.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
(OUT/'WINDOWS.asm').write_text('\n'.join(f"BANK{w['bank']} {w['name']}\n"+'\n'.join(f"{x['CODE']:04X} {x['hex']:<10} {x['text']}" for x in w['instructions']) for w in windows)+'\n', encoding='utf-8')
print(json.dumps({k:result[k] for k in ['source_components_verified','catalogue_EC','catalogue_BIOS','consumer_anchor_bytes_verified','BIOS_modules_verified','OEM_audit_reference_hashes_verified','ROM4_consumer_count']}))
