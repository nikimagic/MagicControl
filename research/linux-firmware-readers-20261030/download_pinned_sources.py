"""Pinned upstream code and one release candidate; no hardware execution."""
import concurrent.futures
import hashlib
import json
import pathlib
import urllib.request

root = pathlib.Path(__file__).resolve().parent
base = root / "sources"
revision = "25457060db45217c74b46f61cc934098ddff8d64"
paths = ["chipsec/hal/intel/spi.py", "chipsec/cfg/8086/rpl.xml",
         "chipsec/hal/amd/psp.py", "chipsec/utilcmd/spi_cmd.py", "drivers/linux/chipsec_km.c"]
items = [("chipsec-" + path.replace("/", "-"),
          "https://raw.githubusercontent.com/chipsec/chipsec/" + revision + "/" + path) for path in paths]
items += [("flashprog-v1.6-rc2.tar.bz2", "https://www.flashprog.org/releases/flashprog-v1.6-rc2.tar.bz2")]

def get(item):
    name, url = item
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "MagicControl-research"}), timeout=20) as response:
            data = response.read(1500001)
        if len(data) > 1500000:
            raise RuntimeError("Source exceeds size limit")
        (base / name).write_bytes(data)
        return dict(file=name, url=url, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    except Exception as error:
        return dict(file=name, url=url, error=str(error))

if __name__ == "__main__":
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        rows = list(pool.map(get, items))
    (root / "pinned-sources.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(rows))
