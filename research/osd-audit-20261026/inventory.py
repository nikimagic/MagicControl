"""Static source inventory; never imports/starts MagicControl or device APIs."""
from pathlib import Path
import hashlib,json,re
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
files=[];calls=[]
pattern=re.compile(r'beginOperation\(|completeOperation\(|(?:osd_|osd|popup)->show\(|osd::(?:system|keyboard|lighting|keySwitch|gpuPower|fanMode|diagnosticDump)')
for path in sorted((ROOT/'src/gui').rglob('*')):
    if path.suffix not in ['.cpp','.h']:continue
    data=path.read_bytes();text=data.decode('utf-8-sig')
    rows=[]
    for line_no,line in enumerate(text.splitlines(),1):
        if pattern.search(line):
            row={'path':path.relative_to(ROOT).as_posix(),'line':line_no,'source':line.strip()}
            rows.append(row);calls.append(row)
    if rows or path.name in ['OsdModel.h','OsdMessages.h','OsdIcon.h','DiagnosticDumpOsd.h']:
        files.append({'path':path.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(data).hexdigest(),'sites':len(rows)})
header=(ROOT/'src/gui/common/DeviceHeader.h').read_text(encoding='utf8')
section=header.split('kSupportedLaptops{{',1)[1].split('}};',1)[0]
models=[]
for line in section.splitlines():
    if 'LaptopSupport::' not in line:continue
    strings=re.findall(r'"([^"]*)"',line)
    expression=line.strip().split(',',1)[0].lstrip('{')
    models.append({'id_or_expression':expression,'strings':strings,'support':re.search(r'LaptopSupport::(\w+)',line).group(1),
                   'scope':'Same OSD factories; actions still require existing actual-device/backend admission. Preview never executes commands.'})
result={'scope':'Static superset of OSD factory/call/adapter sites and catalogue entries, not physical per-model testing.',
        'models':models,'model_count_including_unknown':len(models),'known_model_count':sum(m['support']!='System' for m in models),
        'files':files,'sites':calls}
(OUT/'SOURCE-INVENTORY.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('catalogue entries',len(models),'known',result['known_model_count'],'files',len(files),'OSD source sites',len(calls))
