"""Read-only PRI/XBF extraction. Does not load OEM code or access hardware."""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out' / Path(__file__).resolve().parent.relative_to(ROOT); OUT.mkdir(parents=True, exist_ok=True)
sources = [ROOT / 'research/research-gm6ir0c/battery-ui-resources/resources.pri']
sources += list((ROOT / 'research/devices/gm5hg0a/oem-5.52.49.6/nested').rglob('resources.pri'))
sources += list((ROOT / 'research/new-l16-20261022/appx/CCU.WinUI').rglob('resources.pri'))
evidence = []
for src in sources:
    data = src.read_bytes()
    at = 0
    while (at := data.find(b'XBF\0', at)) >= 0:
        if at + 12 <= len(data):
            metadata, nodes = struct.unpack_from('<II', data, at + 4)
            size = 12 + metadata + nodes
            if size < 8 * 1024 * 1024 and at + size <= len(data):
                xbf = data[at:at + size]
                if 'SingleZoneKeyboardView'.encode('utf-16le') in xbf:
                    digest = hashlib.sha256(xbf).hexdigest()
                    target = OUT / (digest + '.xbf')
                    target.write_bytes(xbf)
                    evidence.append(dict(source=str(src.relative_to(ROOT)), offset=at, size=size,
                                         source_sha256=hashlib.sha256(data).hexdigest(), xbf_sha256=digest))
        at += 4
(OUT / 'resource-evidence.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
print(json.dumps(evidence, indent=2))
