"""Extract the actual native fan records from the saved Wujie EC image.

Offline analysis/data export only. No driver, EC, fan or product invocation.
CSV is a rising-temperature steady-state base request, not live applied PWM.
"""
from pathlib import Path
import csv
import hashlib
import json

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[1]
firmware = ROOT / 'research/wujie-full-bios-20261024/wujie-embedded-ec-core-candidate-128KiB.bin'
image = firmware.read_bytes()
digest = hashlib.sha256(image).hexdigest()
if digest != '5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8':
    raise ValueError('Unreviewed firmware image')
live = json.loads((OUT / 'live-native-fans.json').read_text(encoding='utf-8-sig'))
raw = live[-1]['raw']
if (raw['0x0741']&1 or raw['0x07C6']&4 or raw['0x0726']&128 or
        raw['0x0727']&64 or raw['0x0751']&64):
    raise ValueError('Live snapshot is not the reviewed native Default / APP0 context')
header = (raw['0x08E7'] << 8) | raw['0x08E8']
if header != 0x62C0:
    raise ValueError('Live selector differs from reviewed Wujie table')
cpu = int.from_bytes(image[header:header+2], 'big')
gpu = int.from_bytes(image[header+2:header+4], 'big')
mapping = header + 4
names = ['Gaming / base', 'Office', 'Turbo', 'Turbo APP variant']

def parameters(address):
    a, b, c, d, e, f = image[address:address+6]
    return dict(address=f'CODE0x{address:04X}', raw=[a,b,c,d,e,f],
        mid_temperature_C=a, mid_pwm_percent=b/2,
        upper_temperature_C=c, upper_pwm_percent=d/2,
        low_slope_percent_per_C=e/20, high_slope_percent_per_C=f/20)

def rising_request(temp, data):
    mid, duty, upper, maximum, low_slope, high_slope = data
    if temp < 48:
        return 0  #47C is history-dependent; this export uses the cold/rising branch.
    if temp > upper:
        return maximum
    if temp >= mid:
        return min(maximum, duty + ((temp-mid)*high_slope)//10)
    return 50 + ((temp-48)*low_slope)//10

def second_steady(request, data):
    lower, upper, offset, stop = data
    # Actual code checks BOTH shared request and current primary PWM. Here
    # they are equal: settled state, no cooldown hold/forced floor/smoothing.
    if request < stop:
        return 0
    if lower <= request <= upper:
        return max(lower, request-offset)
    return request

profiles=[]
for mode, name in enumerate(names):
    secondary=list(image[mapping+4*mode:mapping+4*(mode+1)])
    profiles.append(dict(mode_index=mode, name=name,
        cpu=parameters(cpu+6*mode), gpu_input=parameters(gpu+6*mode),
        second_fan=dict(address=f'CODE0x{mapping+4*mode:04X}', raw=secondary,
            correction_range_percent=[secondary[0]/2,secondary[1]/2],
            offset_percentage_points=secondary[2]/2,
            stop_below_request_and_primary_percent=secondary[3]/2)))

mode=1 if raw['0x0751']&128 else 2 if raw['0x049F']&2 and raw['0x0751']&144==16 else 0
result=dict(firmware=dict(path=str(firmware),sha256=digest,
    scope='Saved BIOS-embedded EC payload, not an independent live EC-flash read'),
    evidence='Static decoded calculation plus live selector/current cold-state corroboration; no load test',
    correction='CODE6598/98F8 and mirrors730/7A7 belong to APU power policy, NOT the fan curve',
    selector=dict(header=f'CODE0x{header:04X}', cpu_table=f'CODE0x{cpu:04X}',
        gpu_table=f'CODE0x{gpu:04X}',second_fan_mapping=f'CODE0x{mapping:04X}'),
    profiles=profiles,
    start_stop=dict(start_C=48,minimum_running_pwm_percent=25,
        off_below_C=47,at_47_C='25% if previous base request >=25%, otherwise0%',
        pwm_raw_scale='percent=raw/2; lower/high slopes use unsigned integer product divided by10'),
    live=dict(utc=live[-1]['utc'], profile_mode_index=mode,
        profile=names[mode],app=bool(raw['0x0741']&1),
        native_header=f'0x{header:04X}',native_second_mapping=f"0x{(raw['0x906D']<<8)|raw['0x906E']:04X}",
        cpu_input_C=raw['0x043E'] if mode==2 else raw['0x9178'],
        cpu_input_source='EC43E' if mode==2 else 'EC9178',
        gpu_input_raw=raw['0x044F'],gpu_input_used_by_selected_table=False,
        cpu_base_request_percent=raw['0x0461']/2,gpu_base_request_percent=raw['0x0469']/2,
        shared_request_percent=raw['0x0670']/2,
        actual_pwm_percent=[raw['0x075B']/2,raw['0x075C']/2],
        actual_rpm=[(raw['0x0464']<<8)|raw['0x0465'],(raw['0x046C']<<8)|raw['0x046D']],
        cooldown_raw=raw['0x0986'],startup_raw=raw['0x0672'],
        ec_extra_floor_percent=raw['0x914C']/2,high_temperature_event=bool(raw['0x07A5']&8),
        battery_temperature_C=round(((raw['0x04A3']<<8)|raw['0x04A2'])/10-273.15,2)),
    warnings=['Curves are base requested PWM, not guaranteed applied PWM during transitions',
        'Default common output can combine requests, enforce floors/holdoff and transform second-fan duty',
        'The second fan is not parametrized by ADLX GPU temperature on this selected record',
        'Timing of180/20 counters is not established in seconds',
        'AC/DC may affect temperatures/other constraints; only the observed selector/profile is live-confirmed'])
(OUT/'native-curves-decoded.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
with (OUT/'native-default-base-curves.csv').open('w',newline='',encoding='utf-8') as stream:
    writer=csv.writer(stream)
    writer.writerow(['profile','EC_CPU_temperature_C','primary_base_request_percent','second_steady_base_request_percent'])
    for mode in range(3):
        for temp in range(20,101):
            request=rising_request(temp,profiles[mode]['cpu']['raw'])
            second=second_steady(request,profiles[mode]['second_fan']['raw'])
            writer.writerow([names[mode],temp,request/2,second/2])
print(json.dumps(dict(selector=result['selector'],profiles=profiles,live=result['live']),indent=2))
