"""Static AML/driver/source review. No device handles or ACPI method execution."""
from pathlib import Path
import hashlib,json,re
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
inputs=[
 ('WUJIE_DSDT_AML',ROOT/'build/windows/ddr5-acpi/01_DSDT.dat'),
 ('WUJIE_DSDT_ASL',ROOT/'build/windows/ddr5-acpi/01_DSDT.dsl'),
 ('GM6_saved_DSDT_ASL',ROOT/'build/research-gm6ir0c/samples/DSDT.dsl'),
 ('GM6_audited_DSDT_ASL',ROOT/'build/gm6-healthy-acpi/DSDT-1c2b99426121.dsl'),
 ('driver_10_reference',ROOT/'src/platform/windows/runtime/Uniwill/UWACPIDriver.sys'),
 ('driver_installed_file_only',Path('C:/Windows/System32/drivers/UWACPIDriver.sys')),
 ('driver_bindings',ROOT/'research/wujie-full-bios-20261024/driver-width-review/COMPARISON.json'),
 ('driver_10_ASM',ROOT/'research/wujie-full-bios-20261024/driver-width-review/6e360ee4a0c4.asm'),
 ('policy_source',ROOT/'src/platform/windows/BatteryEnforcementWindows.h'),
 ('percentage_source',ROOT/'src/devices/mechrevo/x5sp4nag/windows/BatteryChargeWindows.cpp'),
 ('UI_source',ROOT/'src/gui/windows/SettingsPage.cpp')]
records=[];methods=[]
for label,path in inputs:
    data=path.read_bytes()
    records.append({'label':label,'path':str(path),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
                    'scope':'Filesystem bytes only; installed SYS not loaded or invoked by this analysis.'})
    if path.suffix=='.dsl':
        text=data.decode('utf8',errors='replace').replace('\r','')
        for name in ['ECRR','MMRB','MMRW','RECM']:
            hit=re.search(r'Method \('+name+r',',text)
            if not hit:continue
            first=text.index('{',hit.start());depth=1;at=first+1
            while depth and at<len(text):
                if text[at]=='{':depth+=1
                elif text[at]=='}':depth-=1
                at+=1
            methods.append({'source':label,'method':name,'line':text.count('\n',0,hit.start())+1,'text':text[hit.start():at]})
bindings=[]
def walk(value,context=''):
    if isinstance(value,dict):
        context=value.get('path',context)
        if value.get('ioctl') in [0x9c40a488,0x9c40a490]:
            bindings.append({'context':context,**{k:v for k,v in value.items() if k in ['ioctl','method','handler_RVA','ACPI_helper_RVA','call_RVA']}})
        for v in value.values():walk(v,context)
    elif isinstance(value,list):
        for v in value:walk(v,context)
walk(json.loads((ROOT/'research/wujie-full-bios-20261024/driver-width-review/COMPARISON.json').read_text(encoding='utf8')))
result={'scope':'Static saved-file analysis. No hardware/driver calls,writer or app execution.',
        'inputs':records,'methods':methods,'driver_bindings':bindings,
        'exact_wujie_aml_sha256':'0171f11732dcba93dfa8037a713c74040363f932daed8d5eb0011779d12d53f5'}
(OUT/'EVIDENCE.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
(OUT/'AML-METHODS.txt').write_text('\n\n'.join(f"{m['source']} line{m['line']} {m['method']}\n{m['text']}" for m in methods),encoding='utf8')
for m in methods:
    if m['method']=='ECRR':print(m['source'],m['line'],m['text'])
print('Bindings',len(bindings))
