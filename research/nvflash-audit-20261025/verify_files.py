"""Inspect build/portable bytes and PE resources; never executes any reader."""
from pathlib import Path
import ast
import hashlib
import json
import re
import struct
import subprocess

root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent
sha = lambda data: hashlib.sha256(data).hexdigest()
build = json.loads((root / 'build/product-build.json').read_text(encoding='utf-8-sig'))
assert build['status'] == 'complete' and build['version'] == '0.1.210'
assert all(sha((root / p).read_bytes()) == v for p, v in build['application_inputs'].items())

# Reuse only the existing pure PE-decoder function, not its old build assertions.
tree = ast.parse((root / 'research/firmware-validator-tests-20261025/verify_release.py').read_text())
function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'pe_resources')
exec(compile(ast.Module(body=[function], type_ignores=[]), 'existing pure PE parser', 'exec'), globals())
catalogue = (root / 'build/windows/generated/MagicControl/EmbeddedRuntimeCatalog.h').read_text()
pins = re.findall(r'\{(\d+),"NvFlash","([^"]+)","([a-f0-9]{64})",(\d+)\}', catalogue)
assert pins
receipt = dict(version=build['version'], hardware_executed=False, product_executed=False,
               vendor_executed=False, tests_run=False, artifacts=[], embedded_nvflash=[])
for platform, names in [('windows', ('MagicControl.exe', 'mctl.exe')), ('linux', ('MagicControl', 'mctl'))]:
    portable = root / 'MagicControl' / platform
    assert (portable / 'build-version.txt').read_text(encoding='utf-8-sig').strip() == build['version']
    built = root / ('build/windows/bin/Release' if platform == 'windows' else 'build/linux/bin')
    for name in names:
        data = (portable / name).read_bytes()
        assert sha(data) == sha((built / name).read_bytes())
        receipt['artifacts'].append(dict(platform=platform, name=name, sha256=sha(data), matches_build=True))
        if platform == 'windows':
            subsystem, resource = pe_resources(data)
            for identity, file, digest, size in pins:
                blob = resource(10, int(identity))
                assert len(blob) == int(size) and sha(blob) == digest
                assert blob == (root / 'src/platform/windows/runtime/NvFlash' / file).read_bytes()
                receipt['embedded_nvflash'].append(dict(binary=name, file=file, sha256=digest, original_bytes=True))
contract = root / 'src/platform/common/NvflashPlatform.h'
prior = subprocess.check_output(['git', 'show', 'ed8e52e:src/platform/common/NvflashPlatform.h'], cwd=root).decode()
assert prior.replace('\r\n', '\n') == contract.read_text()
receipt['read_command_contract_unchanged'] = True
assert sha((root / 'src/platform/windows/runtime/NvFlash/nvflash64.exe').read_bytes()) == 'c0620a7be0ffdad3782d9ddf238d1a19c31b587cbcfee7036819c8a50e2d89cd'
(out / 'verification-210.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
print(json.dumps(dict(version=build['version'], binaries=len(receipt['artifacts']),
                      resource_checks=len(receipt['embedded_nvflash']), read_contract_unchanged=True)))
