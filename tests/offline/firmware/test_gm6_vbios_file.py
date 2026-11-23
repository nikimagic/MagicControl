"""Validate a pinned GM6 dump; never launch the application or a firmware reader."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


reader, fixture = map(Path, sys.argv[1:])
if not fixture.is_file():
    print("SKIP: local GM6 VBIOS fixture unavailable")
    sys.exit(77)

expected = "64c61873c2c31ff73c5dd78ed56b6a4e7a8ac8fec3afbdc3d42ab4b455fd710a"
data = fixture.read_bytes()
assert len(data) == 2048000 and hashlib.sha256(data).hexdigest() == expected


def validate(device):
    return subprocess.run(
        [str(reader), "inspect", "nvidia", str(fixture), device],
        capture_output=True, text=True, encoding="utf-8", timeout=15,
    )


valid = validate("0x28a0")
assert valid.returncode == 0, valid.stderr
assert json.loads(valid.stdout) == {
    "ifr_version": 3, "prefix_bytes": 0x9400,
    "trailing_bytes": 1859584, "images": 2,
}
wrong = validate("0x28a1")
assert wrong.returncode == 4 and "does not match the selected NVIDIA" in wrong.stderr
assert hashlib.sha256(fixture.read_bytes()).hexdigest() == expected
print("PASS: pinned GM6 IFR v3 ROM, expected PCI chain and device mismatch; file unchanged")
