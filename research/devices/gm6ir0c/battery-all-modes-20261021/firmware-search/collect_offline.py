"""Static container inspection only. Never runs or loads investigated PE files."""
from pathlib import Path
from zipfile import ZipFile
from io import BytesIO
import importlib.util
import hashlib
import json
import re
import struct

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[4]
TOOL = Path(r'C:\Users\admin\Documents\mechrevo\Research\BIOS_OTA_Full\tools\extract_static.py')
ARCHIVE = Path(r'C:\Users\admin\Downloads\Telegram Desktop\ControlCenterX-Logs-20261017-215646.zip')

spec = importlib.util.spec_from_file_location('existing_offline_pe_parser', TOOL)
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def save(name, value):
    (HERE/name).write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

ota_path = REPO/'build/research-gm6ir0c/samples/BIOS_OTA-active.exe'
data = ota_path.read_bytes()
pe = parser.PE(data)
resources = parser.resources(pe, HERE/'ota-resources')
assets = parser.compressed(pe, HERE/'ota-compressed')
static = {'path': str(ota_path), 'bytes': len(data), 'sha256': sha(data),
          'resources': resources, 'compressed': assets, 'scope': 'Offline PE resource/zlib scan, not execution'}
save('ota-static.json', static)

needles = {'7z': b'7z\xbc\xaf\x27\x1c', 'cab': b'MSCF', 'ziplocal': b'PK\x03\x04',
           'uefi_fv': b'_FVH', 'uuite': b'UUITE', 'ite_ec': b'ITE EC', 'gmxir': b'GMxIR', 'mro09': b'MRO09'}
end = max(s['raw']+s['raw_size'] for s in pe.sections)
markers = {'file': str(ota_path), 'sha256': sha(data), 'bytes': len(data),
           'sections_raw_end': end, 'tail_bytes': len(data)-end,
           'security_directory': pe.dirs[4],
           'markers': {n: [m.start() for m in re.finditer(re.escape(v), data)][:50] for n,v in needles.items()}}
save('ota-container-markers.json', markers)

zip_candidates=[]
for offset in markers['markers']['ziplocal']:
    fields=struct.unpack_from('<IHHHHHIIIHH', data, offset)
    raw_name=data[offset+30:offset+30+fields[-2]]
    zip_candidates.append({'offset': offset, 'fields': list(fields),
                           'name_preview': repr(raw_name[:90]),
                           'classification': 'Invalid ZIP field ranges; opcode/immediate occurrence, not accepted archive'})
save('ota-zip-marker-headers.json', zip_candidates)

archive_candidates=[]
helpers=[]
seen={}
with ZipFile(ARCHIVE) as outer:
    inner_name=next(n for n in outer.namelist() if n.startswith('research/') and n.endswith('.zip'))
    inner_bytes=outer.read(inner_name)
    with ZipFile(BytesIO(inner_bytes)) as inner:
        entry_count=len(inner.infolist())
        for entry in inner.infolist():
            if entry.is_dir():
                continue
            base=entry.filename.rsplit('/',1)[-1]
            if re.search(r'(bios|firmware|ecfw|ec_fw|flash|\.rom$|\.cap$|\.fd$|\.800$|\.801$|\.bin$)', base, re.I):
                b=inner.read(entry)
                archive_candidates.append({'entry': entry.filename, 'bytes': len(b), 'sha256': sha(b), 'prefix16': b[:16].hex()})
            if not entry.filename.endswith('/UEFI_Firmware.dll'):
                continue
            b=inner.read(entry); digest=sha(b)
            row={'entry': entry.filename, 'bytes': len(b), 'sha256': digest}
            if digest in seen:
                row['same_bytes_as']=seen[digest]
            else:
                seen[digest]=entry.filename
                helper=parser.PE(b)
                dest=HERE/('uefi-'+digest[:12]); dest.mkdir(exist_ok=True)
                row.update(resources=parser.resources(helper,dest/'resources'),
                           compressed_assets=parser.compressed(helper,dest/'compressed'), exports=helper.exports())
            helpers.append(row)
save('archive-payload-inventory.json', {'archive': str(ARCHIVE), 'inner_entry': inner_name,
                                      'inner_sha256': sha(inner_bytes), 'inner_file_count': entry_count,
                                      'candidates': archive_candidates})
save('uefi-helper-static.json', helpers)

evidence={'date':'2026-10-21', 'target':'GM6IR0C, BIOS N.1.05MRO09, EC SMBIOS1.12, Project25',
          'scope':'Offline inspected data only; no investigated executable, OEM API, device, test or build invoked',
          'parser':{'path':str(TOOL),'sha256':sha(TOOL.read_bytes())},
          'artifacts':[]}
candidate = REPO/'build/gm6-battery-firmware-20261021/GMxIR7xN105MRO09_CAP.exe'
candidate_manifest = candidate.parent/'manifest.json'
if candidate.is_file():
    candidate_data = candidate.read_bytes()
    evidence['downloaded_candidate'] = {'path':str(candidate), 'bytes':len(candidate_data),
        'sha256':sha(candidate_data),'mz':candidate_data[:2]==b'MZ','executed':False,
        'applicability':'Filename N105MRO09 and owner14650HX/4060 evidence; board and EC version require static payload verification'}
    if candidate_manifest.is_file():
        save('owner-mro09-manifest.json', json.loads(candidate_manifest.read_text(encoding='utf-8-sig')))
for p in sorted(HERE.rglob('*')):
    if p.is_file() and p.name!='evidence.json':
        b=p.read_bytes(); evidence['artifacts'].append({'path':str(p.relative_to(HERE)),'bytes':len(b),'sha256':sha(b)})
save('evidence.json',evidence)
print('OTA and',len(helpers),'helper copies parsed; exact firmware image not established')
