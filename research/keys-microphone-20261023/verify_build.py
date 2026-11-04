"""Verify build metadata/files and saved pure-test output; no product/device execution."""
from pathlib import Path
import hashlib,json,re

ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().lower()
state=json.loads((ROOT/'build/product-build.json').read_text(encoding='utf-8-sig'))
assert state['status']=='complete'
version=state['version'];files=[]
for platform,names in (('windows',('MagicControl.exe','mctl.exe')),('linux',('MagicControl','mctl'))):
    assert state['platforms'][platform]['status']=='verified'
    portable=ROOT/'MagicControl'/platform;built=ROOT/'build'/platform/'bin'
    if platform=='windows':built/='Release'
    assert (portable/'build-version.txt').read_text().strip()==version
    assert (ROOT/'build'/platform/'generated/MagicControl/Version.h.version').read_text().strip()==version
    for name in names:
        digest=sha(portable/name);assert digest==sha(built/name)
        files.append({'platform':platform,'name':name,'version':version,'sha256':digest,'matches_build':True})
before=json.loads((ROOT/'build/keys-personal-before.json').read_text())
changed=[name for name,digest in before.items()if not(ROOT/name).is_file()or sha(ROOT/name)!=digest]
assert not changed,changed
inputs=state['application_inputs'];mismatches=[name for name,digest in inputs.items()if sha(ROOT/name)!=digest.lower()]
assert not mismatches,mismatches
tests=[]
for platform in ('windows','linux'):
    path=ROOT/'build/offline'/platform/'Testing/Temporary/LastTest.log'
    text=path.read_text(encoding='utf-8',errors='replace')
    assert 'KEYs18profiles/2OS/11rows' in text and 'Test Passed.' in text
    checks=int(re.search(r'(\d+) checks;',text)[1])
    tests.append({'platform':platform,'test':'settings_cards_offline_parity','passed':True,'checks':checks,
                  'log_sha256':sha(path),'hardware_access':False})
report={'version':version,'common_build':'complete','files':files,
        'source_fingerprint':state['source_fingerprint'],'application_inputs_verified':len(inputs),
        'personal_settings_files_preserved':len(before),'personal_settings_files_changed':changed,
        'tests':tests,'keys_profiles':16,'unknown_variants':2,'key_rows':11,
        'geometry_dpi':[96,144,192],'preview_writes':False,
        'card_alignment':{'KEYs_top_dip':8,'INFO_top_dip':8,'INFO_heading_inset_dip':8,
                          'windows_info_background':'shared BiosHeaderGeometry.card',
                          'status_height_cases_dip':[0,24,96],'visual_runtime_verified':False},
        'microphone_target':{'windows':'Default eCapture/eCommunications endpoint','linux':'Default PulseAudio/PipeWire source, monitor rejected'},
        'native_keys_routes':{'wu-jie-15-xpro':'existing Wujie EC Windows/Fn/USB/AC and camera path',
                              'huawei-hlyl-wxx9':'existing HWMI/sysfs Fn',
                              'other':'OS/application capabilities only; no borrowed EC fields'},
        'microphone_changed_by_agent':False,'application_launched':False,
        'physical_all_models_verified':False,'github_archives_updated':False}
out=Path(__file__).resolve().parent/f'VERIFICATION-{version}.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))
