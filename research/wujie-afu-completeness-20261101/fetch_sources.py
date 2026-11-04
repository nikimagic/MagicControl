"""Fetch pinned source texts only; no firmware/driver/tool execution."""
from pathlib import Path
import hashlib
import json
import urllib.request

root = Path(__file__).resolve().parent / 'sources'
for item in json.loads((root / 'INDEX.json').read_text()):
    path = root / item['file']
    assert path.parent == root
    data = path.read_bytes() if path.exists() else urllib.request.urlopen(item['url'], timeout=30).read()
    if hashlib.sha256(data).hexdigest() != item['sha256']:
        raise RuntimeError('Source changed; review instead of silently replacing: ' + item['url'])
    if not path.exists():
        path.write_bytes(data)
    print(item['file'], item['sha256'])
