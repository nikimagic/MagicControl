"""Public candidate downloads for static inspection only; never run AFU."""
import concurrent.futures
import hashlib
import json
import pathlib
import re
import urllib.request

root = pathlib.Path(__file__).resolve().parent
dest = root / "candidates" / "global-search"
dest.mkdir(parents=True, exist_ok=True)
items = [
    ("sonic-celestica-afulnx_64", "https://raw.githubusercontent.com/sonic-net/sonic-buildimage/ec21e592e49c120bef206b4cc7a1601e4ed51fac/platform/broadcom/sonic-platform-modules-cel/ds3000/utils/afulnx_64", 3000000),
    ("sonic-netberg-afulnx_64", "https://raw.githubusercontent.com/sonic-net/sonic-buildimage/ec21e592e49c120bef206b4cc7a1601e4ed51fac/platform/barefoot/sonic-platform-modules-netberg/aurora-610/utils/afulnx_64", 3000000),
    ("aic-downloads.html", "https://www.aicipc.com/downloads/", 6000000),
]

def get(item):
    name, url, limit = item
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "MagicControl-research"}), timeout=30) as response:
            data = response.read(limit+1)
        if len(data) > limit:
            raise RuntimeError("Candidate exceeds limit")
        (dest/name).write_bytes(data)
        versions = sorted(set(m.group().decode() for m in re.finditer(rb"(?<![0-9])5\.[0-9]{1,2}\.[0-9]{1,2}\.[0-9]{2,5}(?![0-9])", data)))
        return dict(file=name, url=url, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), elf=data[:4]==b"\x7fELF", versions=versions)
    except Exception as error:
        return dict(file=name, url=url, error=str(error))

if __name__ == "__main__":
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        rows = list(pool.map(get, items))
    (root/"global-candidates.json").write_text(json.dumps(rows, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(rows))
