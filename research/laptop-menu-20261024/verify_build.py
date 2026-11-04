"""Check saved source/build/package metadata only; never execute the product."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().lower()


state = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
assert state['status'] == 'complete'
version = state['version']
files = []
for platform, names in (
    ('windows', ('MagicControl.exe', 'mctl.exe')),
    ('linux', ('MagicControl', 'mctl')),
):
    assert state['platforms'][platform]['status'] == 'verified'
    portable = ROOT / 'MagicControl' / platform
    built = ROOT / 'build' / platform / 'bin'
    if platform == 'windows':
        built /= 'Release'
    assert (portable / 'build-version.txt').read_text().strip() == version
    assert (ROOT / 'build' / platform / 'generated/MagicControl/Version.h.version').read_text().strip() == version
    for name in names:
        digest = sha(portable / name)
        assert digest == sha(built / name)
        files.append({'platform': platform, 'file': name, 'version': version, 'sha256': digest})

preserved = {}
for category in ('personal', 'hardware'):
    before = json.loads((ROOT / f'build/laptop-menu-{category}-before.json').read_text())
    changes = [name for name, digest in before.items()
               if not (ROOT / name).is_file() or sha(ROOT / name) != digest]
    assert not changes, (category, changes)
    preserved[category] = {'files': len(before), 'changed': changes}

inputs = state['application_inputs']
assert all(sha(ROOT / name) == digest.lower() for name, digest in inputs.items())
header = (ROOT / 'src/gui/common/DeviceHeader.h').read_text(encoding='utf-8')
block = header.split('kSupportedLaptops{{', 1)[1].split('}};', 1)[0]
ids = []
for line in block.splitlines():
    if not line.strip().startswith('{'):
        continue
    ids.append('eluktronics-hydroc16g2' if 'hydroc16g2::profileId' in line
               else re.match(r'\s*\{"([^"]+)"', line)[1])
assert len(ids) == len(set(ids)) == 17
expected = json.loads((ROOT / 'research/ec-platform-catalogue-20261023/charge-project-coverage.json').read_text())['profiles']
assert set(ids) - {'noname'} == {profile['id'] for profile in expected}
original = ['wu-jie-15-xpro', 'huawei-hlyl-wxx9', 'mechrevo-gm6ir0c',
            'noname', 'eluktronics-hydroc16g2', 'mechrevo-gm5hg0a', 'machenike-l16']
assert ids[:7] == original
categories = {name: block.count('LaptopSupport::' + name)
              for name in ('Controls', 'Battery', 'Inventory', 'System')}
assert categories == {'Controls': 5, 'Battery': 1, 'Inventory': 10, 'System': 1}

def committed_text(name):
    return subprocess.check_output(['git', 'show', f'HEAD:{name}'], cwd=ROOT).decode('utf-8')


geometry_path = 'src/gui/common/Geometry.h'
def header_block(text):
    return text.split('struct HeaderGeometry {', 1)[1].split('struct FansGeometry {', 1)[0]


assert header_block((ROOT / geometry_path).read_text()) == header_block(committed_text(geometry_path))
for name in ('src/gui/common/Layout.h', 'src/gui/common/Fonts.h',
             'src/gui/windows/MainWindow.cpp', 'src/gui/linux/MainWindow.cpp'):
    assert (ROOT / name).read_text() == committed_text(name), name

report = {
    'version': version,
    'scope': 'Source/build/package consistency; no product tests or runtime device execution',
    'common_build': state['status'], 'files': files,
    'application_inputs_verified': len(inputs),
    'source_fingerprint': state['source_fingerprint'],
    'preserved': preserved, 'menu_ids_in_index_order': ids,
    'original_indices_preserved': dict(enumerate(original)), 'support_categories': categories,
    'common_header_geometry_and_brand_geometry_unchanged': True,
    'main_window_adapters_layout_constants_and_fonts_unchanged': True,
    'hardware_profiles_protocols_core_platforms_unchanged': True,
    'new_inventory_fan_probes_allowed': False,
    'application_launched': False, 'hardware_access': False, 'new_tests_run': False,
    'runtime_visual_verified': False, 'github_archives_updated': False,
}
out = Path(__file__).with_name(f'VERIFICATION-{version}.json')
out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))
