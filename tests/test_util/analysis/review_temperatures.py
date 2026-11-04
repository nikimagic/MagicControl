"""Extract recorded temperature channels; no device reads or writes.

Usage: python review_temperatures.py [output-directory]
The companion C++ --temperatures replay exercises the production decoder/UI.
"""
from pathlib import Path
from datetime import datetime, timezone
import collections
import csv
import hashlib
import json
import statistics
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "tests/test_util/analysis/temperature_review"
OUT.mkdir(parents=True, exist_ok=True)
FIELDS = ["run", "sample", "utc", "raw_smn", "adl_cpu", "adl_edge", "adl_gfx", "adlx_gpu", "smn", "ec",
          "cpu_expected_ui", "gpu_expected_ui", "old_cpu_ui", "adl_to_smn_ms", "adl_to_adlx_ms"]
rows, sources, unsupported = [], [], collections.Counter()


def display(value):
    return "—" if value is None else f"{value:.1f}".removesuffix(".0").replace(".", ",")


for path in sorted((ROOT / "tests/test_util/logs").glob("*windows*/dump.jsonl")):
    samples = collections.defaultdict(dict)
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for line in stream:
            digest.update(line)
            if not any(token in line for token in
                       [b'TEMPERATURE_', b'CPU Tctl/Tdie', b'GPUTemperature', b'GPUHotspotTemperature', b'ec/0x43E']):
                continue
            d = json.loads(line)
            if d.get("event") != "sensor":
                continue
            channel = None
            if d["source"] == "AMD ADL PMLog" and d["id"].startswith("adapter/0/"):
                channel = {8: "adl_edge", 28: "adl_gfx", 32: "adl_cpu"}.get(int(d["id"].split("/")[-1]))
            elif d["source"] == "AMD ADLX" and d["id"] == "gpu/0/GPUTemperature":
                channel = "adlx_gpu"
            elif d["source"] == "PawnIO / AMD SMN" and d["id"] == "package/smn/59800":
                channel = "smn"
            elif d["source"] == "UWACPIDriver / ECRR" and d["id"] == "ec/0x43E":
                channel = "ec"
            if d.get("status") != "ok" or not isinstance(d.get("value"), (int, float)):
                unsupported[(d["source"], d.get("name", d["id"]))] += 1
                continue
            if channel:
                samples[d["sample"]][channel] = d
    sources.append({"path": str(path.relative_to(ROOT)), "sha256": digest.hexdigest(), "samples": len(samples)})
    for number, sample in sorted(samples.items()):
        row = {"run": path.parent.name, "sample": number}
        for channel, d in sample.items():
            row[channel] = d["value"]
        adl = sample.get("adl_cpu", {})
        row["utc"] = adl.get("utc", "")
        row["raw_smn"] = sample.get("smn", {}).get("raw", "")
        row["cpu_expected_ui"] = display(row.get("smn", row.get("adl_cpu")))
        row["gpu_expected_ui"] = display(row.get("adl_edge", row.get("adl_gfx")))
        row["old_cpu_ui"] = display(row.get("adl_cpu"))
        for channel, field in [("smn", "adl_to_smn_ms"), ("adlx_gpu", "adl_to_adlx_ms")]:
            if adl and channel in sample:
                row[field] = round((sample[channel]["monotonic_us"] - adl["monotonic_us"]) / 1000, 3)
        rows.append(row)

with (OUT / "samples.tsv").open("w", encoding="utf-8", newline="") as stream:
    writer = csv.DictWriter(stream, FIELDS, delimiter="\t")
    writer.writeheader()
    writer.writerows(rows)

cpu_delta = [r["smn"] - r["adl_cpu"] for r in rows if "smn" in r and "adl_cpu" in r]
gpu_delta = [r["adlx_gpu"] - r.get("adl_edge", r.get("adl_gfx")) for r in rows
             if "adlx_gpu" in r and ("adl_edge" in r or "adl_gfx" in r)]
stats = lambda values: {"n": len(values), "min": min(values), "mean": statistics.mean(values), "max": max(values)} if values else {}
summary = {"created_utc": datetime.now(timezone.utc).isoformat(), "samples": len(rows), "inputs": sources,
           "cpu_smn_minus_adl": stats(cpu_delta), "gpu_adlx_minus_adl": stats(gpu_delta),
           "max_source_delay_ms": {field: max((r[field] for r in rows if field in r), default=None)
                                   for field in ["adl_to_smn_ms", "adl_to_adlx_ms"]},
           "unsupported_channels": [{"source": k[0], "name": k[1], "count": v} for k, v in sorted(unsupported.items())]}
(OUT / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({k: summary[k] for k in ["samples", "cpu_smn_minus_adl", "gpu_adlx_minus_adl", "max_source_delay_ms"]}, indent=2))
