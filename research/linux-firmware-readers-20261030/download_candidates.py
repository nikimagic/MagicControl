"""Download candidate artifacts for static inspection, without running readers."""
import concurrent.futures
import hashlib
import json
import pathlib
import urllib.request

root = pathlib.Path(__file__).resolve().parent
base = root / "candidates"
base.mkdir(exist_ok=True)
items = [
    ("jupiter-hw-support-20260930.1-1-any.pkg.tar.zst", "https://steamdeck-packages.steamos.cloud/archlinux-mirror/jupiter-main/os/x86_64/jupiter-hw-support-20260930.1-1-any.pkg.tar.zst", 65000000),
    ("jupiter-hw-support-20260930.1-1-any.pkg.tar.zst.sig", "https://steamdeck-packages.steamos.cloud/archlinux-mirror/jupiter-main/os/x86_64/jupiter-hw-support-20260930.1-1-any.pkg.tar.zst.sig", 100000),
    ("flashprog-v1.5.tar.bz2", "https://flashprog.org/releases/flashprog-v1.5.tar.bz2", 1000000),
    ("flashprog-v1.6-rc2.tar.bz2", "https://flashprog.org/releases/flashprog-v1.6-rc2.tar.bz2", 1000000),
    ("chipsec-tree.json", "https://api.github.com/repos/chipsec/chipsec/git/trees/chipsec2?recursive=1", 3000000),
    ("flashrom-releases.json", "https://api.github.com/repos/flashrom/flashrom/releases/latest", 1000000),
    ("chipsec-version.txt", "https://raw.githubusercontent.com/chipsec/chipsec/chipsec2/chipsec/VERSION", 10000),
]

def get(item):
    name, url, limit = item
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "MagicControl-research"})
        with urllib.request.urlopen(request, timeout=40) as response:
            data = response.read(limit+1)
            final_url = response.url
        if len(data) > limit:
            raise RuntimeError("Artifact exceeds research size limit")
        (base / name).write_bytes(data)
        return dict(file=name, url=url, final_url=final_url, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    except Exception as error:
        return dict(file=name, url=url, error=str(error))

if __name__ == "__main__":
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        rows = list(pool.map(get, items))
    (root / "candidates.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(rows))
