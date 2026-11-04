"""Fetch public research artifacts only; never execute downloaded hardware tools."""
import concurrent.futures
import hashlib
import json
import pathlib
import urllib.request

root = pathlib.Path(__file__).resolve().parent
base = root / "sources"
base.mkdir(exist_ok=True)
items = [
    ("flashrom-chipset_enable.c", "https://raw.githubusercontent.com/flashrom/flashrom/main/chipset_enable.c"),
    ("flashrom-sb600spi.c", "https://raw.githubusercontent.com/flashrom/flashrom/main/sb600spi.c"),
    ("amd-flashrom-chipset_enable.c", "https://raw.githubusercontent.com/amd/flashrom-amd/main/chipset_enable.c"),
    ("chipsec-spi.py", "https://raw.githubusercontent.com/chipsec/chipsec/chipsec2/chipsec/hal/spi.py"),
    ("kernel-spi-amd.c", "https://raw.githubusercontent.com/torvalds/linux/master/drivers/spi/spi-amd.c"),
    ("kernel-ec_sys.c", "https://raw.githubusercontent.com/torvalds/linux/master/drivers/acpi/ec_sys.c"),
    ("valve-package-index.html", "https://steamdeck-packages.steamos.cloud/archlinux-mirror/jupiter-main/os/x86_64/"),
    ("flashprog-amd_rom3read.c.base64", "https://review.sourcearcade.org/plugins/gitiles/flashprog/+/refs/heads/1.6.x/amd_rom3read.c?format=TEXT"),
    ("flashprog-amd_spi100.c.base64", "https://review.sourcearcade.org/plugins/gitiles/flashprog/+/refs/heads/1.6.x/amd_spi100.c?format=TEXT"),
]

def get(item):
    name, url = item
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "MagicControl-research"})
        with urllib.request.urlopen(request, timeout=25) as response:
            data = response.read(3000001)
            final_url = response.url
        if len(data) > 3000000:
            raise RuntimeError("Research source exceeds size limit")
        (base / name).write_bytes(data)
        return dict(file=name, url=url, final_url=final_url, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    except Exception as error:
        return dict(file=name, url=url, error=str(error))

if __name__ == "__main__":
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        rows = list(pool.map(get, items))
    (root / "sources.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(rows))
