"""Check file provenance and catalogue coverage only; never execute firmware."""
from pathlib import Path
import sys as _resolver_sys
_resolver_sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'ec-platform-catalogue-20261023'))
from relocated_evidence import read_verified
import json,hashlib,re,uuid
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
OUT=ROOT/'out/battery-logger-comparison-20261026'/('provenance-'+uuid.uuid4().hex);OUT.mkdir(parents=True,exist_ok=False)
sha=lambda b:hashlib.sha256(b).hexdigest()
census=json.loads((HERE/'census.json').read_text(encoding='utf8'))
spi=ROOT/'research/windows-full-spi-20261024/runs/afu-20261024-185128471/bios-32MiB.bin'
raw=spi.read_bytes();verify=spi.with_name('bios-32MiB-verify.bin').read_bytes()
assert raw==verify and len(raw)==33554432 and sha(raw)=='2870a75a8c2518199cd9c572c3cd466d9f971b41bb047edbd3dda85ec6d2aaad'
payload=ROOT/'research/wujie-full-bios-20261024/wujie-embedded-ec-pd-256KiB.bin';p=payload.read_bytes()
assert sha(p)=='a813b3ed9d2978d3413ce3f25c253425480152e2def7debb29d7ca33ac471bfa'
assert sha(p[:131072])==census['firmware'][0]['sha256']
offsets=[];i=0
while (i:=raw.find(p,i))>=0:offsets.append(i);i+=1
assert offsets==[0x137db4c,0x13bdb6c]
units=json.loads((ROOT/'research/ec-platform-catalogue-20261023/source-units.json').read_text(encoding='utf8'))
gm=[]
for unit in units:
 if unit['kind']!='BIOS' or unit['source_label']not in ('GM6IR0C user SPI backup N.1.05MRO09','GM6IR0C user patched SPI new.fd N.1.05MRO09','MRO10 owner archive [backup.fd]'):continue
 b,_=read_verified(unit['path'],unit['sha256'])
 gm.append(dict(source=unit['source_label'],path=unit['path'],sha256=unit['sha256'],EC_core_literal_offsets={f['label']:b.find(Path(f['path']).read_bytes())for f in census['firmware']if f['model']=='GM6'},scope='Literal match only; compressed/unextracted absence is not global absence. Patched/family firmware is not active flash attestation.'))
sources=[]
for p in [ROOT/'build/windows/ddr5-acpi/01_DSDT.dsl',ROOT/'build/windows/ddr5-acpi/01_DSDT.dat',ROOT/'../Decompile/GM6IR0C/GM6_ACPI_Healthy_20261021/DSDT-1c2b99426121.dsl',ROOT/'src/core/include/MagicControl/BatteryJournal.h',ROOT/'src/core/include/MagicControl/Monitoring.h',ROOT/'src/platform/windows/MonitoringWindows.cpp',ROOT/'research/devices/gm6ir0c/battery-all-modes-20261021/ec-audit/HEALTHY.md',ROOT/'research/gm6-current-idle-20261022/REPORT.md']:
 b=p.read_bytes();sources.append(dict(path=str(p),bytes=len(b),sha256=sha(b)))
windows=json.loads((HERE/'curated-windows.json').read_text(encoding='utf8'));assert len(windows)==32 and all(w['aligned_end']for w in windows)
bitdefs=[]
for group,path in [('WUJIE',ROOT/'src/protocols/uniwill/x5sp4nag/BatteryCandidates.h'),('GM6',ROOT/'src/protocols/uniwill/gm6ir0c/BatteryDecode.h')]:
 text=path.read_text(encoding='utf-8-sig');tail=text.split('batteryCandidateBits[]{',1)[1].split('};',1)[0]
 for m in re.finditer(r'\{(0x[\da-fA-F]+),(\d+),"([^"]+)"\}',tail):
  bitdefs.append(dict(model=group,address=int(m[1],16),bit=int(m[2]),logger_label=m[3],meaning='See FIELD-MAP and curated owner/consumer; a question mark remains unresolved unless explicitly proved.'))
assert len(bitdefs)==19
fields=json.loads((HERE/'field-map.json').read_text(encoding='utf8'))['rows'];assert len(fields)==103
result=dict(scope='Offline verification of saved files and analysis structure, not tests of the firmware/hardware',WUJIE_SPI=dict(path=str(spi),sha256=sha(raw),two_reads_equal=True,bytes=len(raw),payload_sha256=sha(payload.read_bytes()),payload_offsets=offsets),GM6_BIOS=gm,supplemental_sources=sources,logger_field_counts={k:len(v)for k,v in census['catalogues'].items()},candidate_bit_definitions=bitdefs,curated_windows=len(windows),all_requested_endpoints_on_decoded_instruction_boundaries=True,complete_field_coverage=True,complete_firmware_semantics=False,hardware_executed=False,product_changed=False)
(OUT/'VERIFICATION.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'counts':result['logger_field_counts'],'bits':len(bitdefs),'GM6_BIOS':[(x['source'],x['EC_core_literal_offsets'])for x in gm],'WUJIE_payload_offsets':[hex(x)for x in offsets],'hardware_executed':False},ensure_ascii=False))
