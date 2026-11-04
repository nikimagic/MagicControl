"""Saved HPBSD429 logs/dumps only. No application, device or vendor execution."""
from pathlib import Path
from collections import Counter
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
CAPTURE = ROOT / 'Users_logs/MINISFORUM_HPBSD/20261101-124717-MSK'
SNAPSHOT = CAPTURE / 'snapshot'
LOGS = SNAPSHOT / 'logs_HPBSD'
EC = LOGS / 'ec-dumps/dump-20261101-093618-0'
BIOS = LOGS / 'bios-dumps/bios-20261101-093732-0'
sha = lambda b: hashlib.sha256(b).hexdigest()
image, second = (BIOS / 'bios.bin').read_bytes(), (BIOS / 'bios-verify.bin').read_bytes()
assert len(image) == 33554432 and image == second
assert sha(image) == '75220b7b7f684a27b3d1241a4dcb4a71800ab5b1722176050f2f8faf4d4d3103'
ram, valid = (EC / 'ec.bin').read_bytes(), (EC / 'ec-valid.bin').read_bytes()
assert len(ram) == len(valid) == 4096 and all(valid)
embedded_ec = (ROOT / 'research/minipc-hpbsd-20261026/firmware-analysis/ec-candidate-first128k.bin').read_bytes()
assert image[:len(embedded_ec)] == embedded_ec
smm = (OUT / 'FchTacomaSmmInit/body.bin').read_bytes()
assert sha(smm) == '1a7e674a1da97adcf06721b0f69cf20b7dc49890bc01ef49aa4c2cec1d1095fa'
channels = json.loads((EC / 'sensor-summary.json').read_text())['channels']
metrics = [c for c in channels if c['device'] == 'APU' or
           (c['device'].startswith('CPU /') and c['metric'] == 'power')]
limits = {c['metric']: c for c in metrics if c['metric'].endswith('_limit')}
assert limits['Fast_limit']['minimum'] == limits['Fast_limit']['maximum'] == 65
assert abs(limits['STAPM_limit']['last'] - 54.4) < .001
assert abs(limits['Slow_limit']['last'] - 60) < .001
events, operations, lowlevel, commands = Counter(), Counter(), Counter(), []
for line in (LOGS / 'MagicControl-1.jsonl').open(encoding='utf-8-sig', errors='replace'):
    try:
        row = json.loads(line)
    except ValueError:
        continue  # External copy can end in an incomplete append-only row.
    events[(row.get('channel'), row.get('event'))] += 1
    if row.get('event') == 'command_requested':
        operations[row.get('command')] += 1
        if row.get('command') == 'pawnio.lpc':
            lowlevel[row.get('action_id')] += 1
    if row.get('event') == 'command_result' and row.get('command') in ('fans.execute', 'info.ec_dump', 'info.bios_dump'):
        commands.append({k: row[k] for k in ('utc', 'command', 'outcome', 'elapsed_us', 'message') if k in row})
fan_results = [r for r in commands if r['command'] == 'fans.execute']
assert len(fan_results) == 5 and all(r['outcome'] == 'readback_confirmed' for r in fan_results)
result = dict(
    scope='File analysis of owner-generated0.1.429 captures; no hardware writes or new dumps',
    capture_path=str(CAPTURE.relative_to(ROOT)),
    bios=dict(bytes=len(image),sha256=sha(image),two_passes_equal=True,
              capture_kind='declared_rom_image',physical_spi_complete=None,
              embedded_ec_sha256=sha(embedded_ec),embedded_ec_equal_prior=True,
              embedded_ec_is_not_live_EC_flash=True,smm_sha256=sha(smm),smm_equal_prior=True),
    ec=dict(bytes=len(ram),valid_bytes=sum(bool(x) for x in valid),sha256=sha(ram),
            selector=ram[0x32f],power_event_byte=ram[0x361],cpu_temperature=ram[0x309],system_temperature=ram[0x305]),
    default_tables=json.loads((EC / 'fan-tables-default.json').read_text()),
    ec_dump_readonly=True,fan_commands=fan_results,
    editor=dict(cpu_ordinary_points=7,system_ordinary_points=2,hidden_last_row='immutable hot MAX, not a missing ordinary point',
                temperature_input_bounds=[20,100],percent_input_bounds=[5,100],
                manual_hot_stage_from_saved_draft=dict(cpu=81,system=80)),
    apu=metrics,
    power_control=dict(public_writer_connected=False,displayed_25_25_30_are_not_live_limits=True,
                       measured_factory_stapm_slow_fast=[54.4,60,65],
                       conditional_B2_stapm_slow_fast_from_unchanged_BIOS=[70.8,70.8,70.8],B2_measured=False,
                       prior_private_test_setter_bounds=[5,65],no_imported_Wujie_or_Honor_writer=True),
    npu=dict(present=True,inventory_name='NPU Compute Accelerator Device',driver='32.0.20102.3930',
             installed_xrt_file_version='32.00.20102.3931',
             readonly_XRT_platform='NPU Phoenix; Power Mode Default; Total Columns5; Estimated Power N/A',
             activity_frequency_from_ADL_ADLX_unavailable=True,unsupported_measurements_are_not_zero=True),
    detailed_LPC_events=dict(command_pairs=operations['pawnio.lpc'],by_action=dict(lowlevel),
                             cause='explicit dump/control raw transport audit, not passive polling'),
    application_code_modified=False,hardware_payloads_written=False)
(OUT / 'evidence.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps(dict(bios_sha256=sha(image),ec_valid_bytes=sum(bool(x) for x in valid),
                     confirmed_fan_commands=len(fan_results),live_limits=[54.4,60,65],
                     NPU='Phoenix present; XRT Estimated Power=N/A',LPC_command_pairs=operations['pawnio.lpc']),indent=2))
