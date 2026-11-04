"""Offline search of captured channels and EC numeric representations.

Uses per-byte times and interpolates each reference inside its own run.
Matches are candidates, never automatic sensor identities. No hardware access.
"""
from pathlib import Path
from datetime import datetime
import collections
import csv
import json
import re
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "tests/test_util/analysis/firmware_temperatures"
OUT.mkdir(exist_ok=True)
CHANNELS = {
    ("AMD ADL PMLog", "adapter/0/sensor/32"): "CPU_ADL",
    ("AMD ADL PMLog", "adapter/0/sensor/28"): "GPU_GFX",
    ("AMD ADL PMLog", "adapter/0/sensor/29"): "SoC",
    ("AMD ADLX", "gpu/0/GPUTemperature"): "GPU_ADLX",
    ("PawnIO / AMD SMN", "package/smn/59800"): "CPU_Tctl",
    ("AMD ADLX", "gpu/0/NPUActivityLevel"): "NPU_activity",
    ("AMD ADLX", "gpu/0/NPUFrequency"): "NPU_frequency",
}
series = collections.defaultdict(lambda: collections.defaultdict(list))
frames, pm_status, unknown, xrt_thermal = [], collections.Counter(), set(), set()
sensor_ranges = collections.defaultdict(list)


def timestamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


for path in sorted((ROOT / "tests/test_util/logs").glob("*windows*/dump.jsonl")):
    for line in path.open(encoding="utf-8"):
        if '"event":"sensor"' in line:
            d = json.loads(line)
            key = (d["source"], d["id"])
            metric = CHANNELS.get(key)
            if d["source"] == "AMD ADL PMLog" and d["id"].startswith("adapter/0/"):
                name = d.get("name", "")
                if name.startswith("TEMPERATURE_"):
                    pm_status[(name, d["status"])] += 1
                if name == "UNKNOWN_PMLOG_SENSOR" and (d.get("raw") or {}).get("supported"):
                    unknown.add(d["id"])
            if d["source"] == "AMD XRT":
                raw = d.get("raw") or {}
                text = raw.get("stdout_stderr", "") if isinstance(raw, dict) else str(raw)
                xrt_thermal.update(s.strip() for s in text.splitlines() if re.search("temp|thermal|sensor", s, re.I))
            if "MSAcpi_ThermalZoneTemperature" in d["source"] and d.get("name") == "CurrentTemperature":
                metric = "WMI_" + ("ECTZ" if "ECTZ" in d["id"] else "TZ01")
            if metric and d.get("status") == "ok" and isinstance(d.get("value"), (int, float)):
                series[path.parent.name][metric].append((timestamp(d["utc"]), d["value"]))
                sensor_ranges[metric].append(d["value"])
        elif '"event":"ec_dump"' in line:
            d = json.loads(line)
            raw, valid = bytearray(4096), bytearray(4096)
            times = np.full(4096, np.nan)
            for byte in d["bytes"]:
                a, v = byte["address"], byte.get("raw")
                if 0 <= a < 4096 and isinstance(v, int):
                    raw[a] = v
                    valid[a] = 1
                    times[a] = timestamp(byte["utc"])
            frames.append({"run": path.parent.name, "sample": d["sample"], "raw": raw, "valid": valid, "times": times})

raw = np.array([list(f["raw"]) for f in frames], dtype=np.uint8)
valid = np.array([list(f["valid"]) for f in frames], dtype=bool)
times = np.array([f["times"] for f in frames])
reference_names = ["CPU_Tctl", "CPU_ADL", "GPU_GFX", "SoC"]
references = {name: np.full(times.shape, np.nan) for name in reference_names}
for run, values in series.items():
    indexes = [i for i, frame in enumerate(frames) if frame["run"] == run]
    for name in reference_names:
        pairs = sorted(values.get(name, []))
        if len(pairs) < 2:
            continue
        x, y = np.array(pairs).T
        references[name][indexes] = np.interp(times[indexes], x, y, left=np.nan, right=np.nan)

matches, format_counts, sparse = [], [], []
formats = ["u1", "i1", "<u2", ">u2", "<i2", ">i2", "<u4", ">u4", "<i4", ">i4",
           "<u8", ">u8", "<i8", ">i8", "<f4", ">f4", "<f8", ">f8"]
for fmt in formats:
    dtype = np.dtype(fmt)
    width = dtype.itemsize
    columns = raw.shape[1] - width + 1
    decoded = np.ndarray((len(frames), columns), dtype=dtype, buffer=raw.data, strides=(raw.shape[1], 1)).astype(float)
    ok = valid[:, :columns].copy()
    for extra in range(1, width):
        ok &= valid[:, extra:extra + columns]
    scales = [1.0] if dtype.kind == "f" else [1.0, .5, .1, .01, 1 / 256]
    for scale in scales:
        v = decoded * scale
        finite = ok & np.isfinite(v) & (np.abs(v) < 100000)
        in_range = finite & (v >= 30) & (v <= 70)
        # Dynamic candidates in the requested range, not arbitrary IEEE bit patterns.
        with np.errstate(invalid="ignore", over="ignore"):
            extent = np.nanmax(np.where(in_range, v, np.nan), axis=0) - np.nanmin(np.where(in_range, v, np.nan), axis=0)
            minimum = np.nanmin(np.where(in_range, v, np.nan), axis=0)
            maximum = np.nanmax(np.where(in_range, v, np.nan), axis=0)
        hits = in_range.sum(axis=0)
        candidates = np.flatnonzero((hits >= max(3, .05 * len(frames))) & (extent >= 1))
        label = f"{fmt}*{scale:g}"
        format_counts.append({"format": label, "locations_with_any_30_70_hit": int(np.count_nonzero(hits)),
                              "locations_with_3plus_hits": int(np.count_nonzero(hits >= 3)),
                              "dynamic_3plus_hit_candidates": len(candidates),
                              "dynamic_in_80pct_of_frames": int(np.count_nonzero((hits >= .8 * len(frames)) & (extent >= 1)))})
        for address in candidates:
            values = v[:, address]
            row = {"address": f"0x{address:03X}", "format": label, "width": width,
                   "valid_frames": int(finite[:, address].sum()), "range_30_70_frames": int(hits[address]),
                   "range_minimum": float(minimum[address]), "range_maximum": float(maximum[address]),
                   "overlaps_CPTM": bool(address <= 0x43E < address + width),
                   "known_fan_table": bool(0xF00 <= address < 0xF60)}
            reference_matches = []
            for name, reference in references.items():
                ref = reference[:, address]
                paired = in_range[:, address] & np.isfinite(ref)
                if paired.sum() < 30 or np.std(values[paired]) < .3 or np.std(ref[paired]) < .3:
                    continue
                error = values[paired] - ref[paired]
                match = {"reference": name, "address": f"0x{address:03X}", "format": label,
                         "width": width, "frames": int(paired.sum()), "MAE_C": float(np.mean(np.abs(error))),
                         "correlation": float(np.corrcoef(values[paired], ref[paired])[0, 1]),
                         "minimum": float(values[paired].min()), "maximum": float(values[paired].max()),
                         "overlaps_CPTM": bool(address <= 0x43E < address + width),
                         "known_fan_table": bool(0xF00 <= address < 0xF60)}
                matches.append(match)
                reference_matches.append(match)
            if reference_matches:
                best = min(reference_matches, key=lambda item: item["MAE_C"])
                row.update({"closest_reference": best["reference"], "reference_frames": best["frames"],
                            "MAE_C": best["MAE_C"], "correlation": best["correlation"]})
            sparse.append(row)

matches.sort(key=lambda m: (m["reference"], m["MAE_C"], -m["correlation"]))
with (OUT / "ec_temperature_candidates.csv").open("w", encoding="utf-8", newline="") as stream:
    writer = csv.DictWriter(stream, list(matches[0]) if matches else ["reference"])
    writer.writeheader()
    writer.writerows(matches)
with (OUT / "ec_temperature_sparse_candidates.csv").open("w", encoding="utf-8", newline="") as stream:
    writer = csv.DictWriter(stream, list(sparse[0]) if sparse else ["address"])
    writer.writeheader()
    writer.writerows(sparse)

registers = {}
for address in [0x43E, 0x44F, 0x77, 0x78, 0x79, 0x7A, 0x477, 0x478, 0x479, 0x47A]:
    vals = raw[:, address][valid[:, address]]
    registers[f"0x{address:03X}"] = {"frames": len(vals), "min": int(vals.min()), "max": int(vals.max()),
                                    "unique_values": sorted(set(map(int, vals)))}

summary = {"frames": len(frames), "reference_time_alignment": "per EC byte, linear interpolation within each run; no extrapolation",
           "candidate_warning": "Numeric resemblance and correlation do not establish hardware identity",
           "channels": {k: {"samples": len(v), "min": min(v), "max": max(v)} for k, v in sorted(sensor_ranges.items())},
           "pm_temperature_support": [{"name": k[0], "status": k[1], "samples": v} for k, v in sorted(pm_status.items())],
           "supported_unknown_pm_slots": sorted(unknown), "XRT_thermal_lines": sorted(xrt_thermal),
           "registers": registers, "formats": format_counts,
           "best_matches": {name: [m for m in matches if m["reference"] == name][:12] for name in reference_names}}
(OUT / "thermal_channels.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({k: summary[k] for k in ["frames", "channels", "supported_unknown_pm_slots", "XRT_thermal_lines"]}, ensure_ascii=False, indent=2))
for name in reference_names:
    print(name, summary["best_matches"][name][:2])
