"""Read the public driver catalog used by mechrevo.com; never run packages."""
import concurrent.futures
import datetime
import json
import math
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import urlencode

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parent
OUT = ROOT / "sources" / "driver-pages-500"
OUT.mkdir(parents=True, exist_ok=True)
BASE = "https://gwapi.mechrevo.com/api/v1/client/drivers"
LIMIT = 500


def read_page(page):
    url = BASE + "?" + urlencode({"limit": LIMIT, "page": page})
    stem = OUT / f"page-{page}"
    response = subprocess.run([
        "curl.exe", "--silent", "--show-error", "--location", "--compressed",
        "--connect-timeout", "8", "--max-time", "35", "--max-filesize", "20000000",
        "--header", "Accept: application/json", "--dump-header", str(stem) + ".headers.txt",
        "--output", str(stem) + ".json", "--write-out", "%{http_code}", url,
    ], capture_output=True, timeout=45)
    metadata = {
        "url": url, "page": page, "exit_code": response.returncode,
        "http_code": response.stdout.decode("ascii", errors="replace").strip(),
        "error": response.stderr.decode("utf-8", errors="replace").strip(),
        "fetched_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    }
    body = Path(str(stem) + ".json")
    data = None
    try:
        data = json.loads(body.read_text(encoding="utf-8"))
        result = data.get("result", {})
        metadata.update(total=result.get("total"), records=len(result.get("objects", [])),
                        status=data.get("status"), limit=result.get("limit"))
    except (OSError, ValueError) as exc:
        metadata["parse_error"] = str(exc)
    Path(str(stem) + ".request.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps(metadata, ensure_ascii=False), flush=True)
    if response.returncode or metadata["http_code"] != "200" or not data or data.get("status") != "SUCCESS":
        raise RuntimeError(f"Public API page {page} did not return a complete successful response")
    return page, data["result"]


page1, first = read_page(1)
pages = {page1: first}
page_count = math.ceil(first["total"] / first.get("limit", LIMIT))
if page_count > 20:
    raise RuntimeError(f"Catalog requires {page_count} pages; inspect before expanding beyond 20")
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    for page, data in pool.map(read_page, range(2, page_count + 1)):
        pages[page] = data
objects = [obj for page in sorted(pages) for obj in pages[page]["objects"]]
pattern = re.compile(r"bios|uefi|mro\d+|_cap[._]|固件", re.I)
matches = []
for obj in objects:
    searchable = json.dumps(obj, ensure_ascii=False)
    if pattern.search(searchable):
        matches.append({key: obj.get(key) for key in (
            "id", "title", "sub_title", "author", "source_url", "content", "published_at",
            "driver_category_names", "display_product_ids", "product_sku_ids")})
report = {
    "source": BASE, "reported_total": first["total"], "retrieved_records": len(objects),
    "unique_record_ids": len({obj["id"] for obj in objects}), "pages": page_count,
    "complete_page_scan": len(objects) == first["total"], "firmware_matches": matches,
}
(ROOT / "official-firmware-records.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print("CATALOG_SCAN", json.dumps(report, ensure_ascii=False), flush=True)
