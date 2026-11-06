"""Search the 4 KiB EC dumps for registers that track a known temperature.

Method: every EC byte is stored with its own UTC stamp, so a multi-second frame
is not treated as atomic. For each candidate reference channel (CPU, GPU, memory,
disk) the reference is interpolated at each byte's own time and the correlation
is measured byte by byte. A register that merely happens to sit in a plausible
range is not evidence; only a repeatable temporal match is reported.

This is an offline search over stored logs. No hardware access.

Usage:
    python research/test_util/analysis/ec_temperature_match.py
"""
from pathlib import Path
from datetime import datetime
import collections
import json
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
LOGS = ROOT / "out/test_util/logs"
OUT = ROOT / "out/test_util/generated/ec_temperature_match"

# Reference channels, by the exact (source, id) pair used in the logs.
REFERENCES = {
    "CPU_Tctl": ("PawnIO / AMD SMN", "package/smn/59800"),
    "CPU_ADL": ("AMD ADL PMLog", "adapter/0/sensor/32"),
    "GPU_GFX": ("AMD ADL PMLog", "adapter/0/sensor/28"),
    "SoC_ADL": ("AMD ADL PMLog", "adapter/0/sensor/29"),
    "DDR5_50": ("PawnIO SMBus", "smbus/port0/0x50/temperature"),
    "DDR5_51": ("PawnIO SMBus", "smbus/port0/0x51/temperature"),
    "SSD_0": ("Windows NVMe SMART", "PhysicalDrive0/temperature/0"),
    "SSD_2": ("Windows NVMe SMART", "PhysicalDrive0/temperature/2"),
}

# Registers already identified, so they can be excluded from "new" findings.
KNOWN = {0x43E: "CPTM / CPU thermal-control (ACPI)"}
EC_SIZE = 4096
MIN_FRAMES = 6          # a match on fewer frames is not repeatable
MIN_CORRELATION = 0.98  # deliberately strict
PLAUSIBLE = (20.0, 80.0)


def timestamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def load_run(path):
    """Returns (reference series, EC frames) for one dump file."""
    series = collections.defaultdict(list)
    frames = []
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if '"event":"sensor"' in line:
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if record.get("status") != "ok":
                    continue
                value = record.get("value")
                if not isinstance(value, (int, float)) or isinstance(value, bool):
                    continue
                key = (record.get("source"), record.get("id"))
                for name, wanted in REFERENCES.items():
                    if key == wanted:
                        series[name].append((timestamp(record["utc"]), float(value)))
            elif '"event":"ec_dump"' in line:
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                raw = np.zeros(EC_SIZE, dtype=np.int16)
                valid = np.zeros(EC_SIZE, dtype=bool)
                times = np.full(EC_SIZE, np.nan)
                for byte in record.get("bytes", []):
                    address = byte.get("address")
                    value = byte.get("raw")
                    if isinstance(address, int) and 0 <= address < EC_SIZE and isinstance(value, int):
                        raw[address] = value
                        valid[address] = True
                        times[address] = timestamp(byte["utc"])
                frames.append({"raw": raw, "valid": valid, "times": times})
    return series, frames


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    files = sorted(LOGS.glob("*windows*/dump.jsonl"))
    print(f"windows dumps with EC data: {len(files)}")

    # results per reference: address -> aggregated statistics
    findings = collections.defaultdict(dict)

    for path in files:
        series, frames = load_run(path)
        if not frames:
            continue
        usable = {name: sorted(values) for name, values in series.items() if len(values) >= MIN_FRAMES}
        if not usable:
            continue
        print(f"  {path.parent.name}: {len(frames)} EC frames, "
              f"references {sorted(usable)}")

        # Stack the frames: one row per frame, one column per address.
        raw = np.array([frame["raw"] for frame in frames])
        valid = np.array([frame["valid"] for frame in frames])
        times = np.array([frame["times"] for frame in frames])

        for name, pairs in usable.items():
            ref_time = np.array([p[0] for p in pairs])
            ref_value = np.array([p[1] for p in pairs])

            for address in range(EC_SIZE):
                column_valid = valid[:, address]
                if column_valid.sum() < MIN_FRAMES:
                    continue
                column = raw[column_valid, address].astype(float)
                when = times[column_valid, address]
                if not np.all(np.isfinite(when)):
                    continue
                # Interpolate the reference at each byte's own timestamp; outside
                # the reference range the byte carries no information.
                reference = np.interp(when, ref_time, ref_value, left=np.nan, right=np.nan)
                mask = np.isfinite(reference)
                if mask.sum() < MIN_FRAMES:
                    continue
                sample = column[mask]
                target = reference[mask]
                if sample.std() == 0 or target.std() == 0:
                    continue
                correlation = float(np.corrcoef(sample, target)[0, 1])
                if not np.isfinite(correlation) or correlation < MIN_CORRELATION:
                    continue
                # Slope near 1 with a near-zero offset means the register holds the
                # temperature itself rather than a scaled copy of it.
                slope, offset = np.polyfit(target, sample, 1)
                if not (0.9 <= slope <= 1.1):
                    continue
                if not (PLAUSIBLE[0] <= sample.min() and sample.max() <= PLAUSIBLE[1]):
                    continue
                key = (path.parent.name, name)
                previous = findings[name].get(address)
                entry = {
                    "frames": int(mask.sum()),
                    "correlation": correlation,
                    "slope": float(slope),
                    "offset": float(offset),
                    "min": float(sample.min()),
                    "max": float(sample.max()),
                    "run": path.parent.name,
                }
                if previous is None or entry["correlation"] > previous["correlation"]:
                    findings[name][address] = entry
                del key

    report = OUT / "report.md"
    with report.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Поиск температур памяти и диска в прошивке EC\n\n")
        handle.write("Метод: каждый байт EC имеет собственное время съёмки, поэтому "
                     "многосекундный снимок не считается мгновенным. Опорный канал "
                     "интерполируется на время каждого байта, затем считается "
                     "корреляция по кадрам. Совпадение по диапазону без совпадения "
                     "по времени датчиком не считается.\n\n")

        total = 0
        for name in REFERENCES:
            entries = findings.get(name, {})
            handle.write(f"## {name}\n\n")
            if not entries:
                handle.write("Совпадений не найдено.\n\n")
                continue
            handle.write("| Адрес EC | Кадров | Корреляция | Наклон | Смещение | Мин | Макс | Запуск |\n")
            handle.write("|---|---:|---:|---:|---:|---:|---:|---|\n")
            for address in sorted(entries):
                entry = entries[address]
                marker = ""
                if address in KNOWN:
                    marker = f" — известно: {KNOWN[address]}"
                handle.write(f"| 0x{address:03X}{marker} | {entry['frames']} | "
                             f"{entry['correlation']:.4f} | {entry['slope']:.3f} | "
                             f"{entry['offset']:+.2f} | {entry['min']:.2f} | "
                             f"{entry['max']:.2f} | {entry['run']} |\n")
                total += 1
            handle.write("\n")

        handle.write(f"\nВсего совпадений: {total}.\n")

    # Machine-readable form for the next step.
    with (OUT / "matches.json").open("w", encoding="utf-8", newline="\n") as handle:
        json.dump({name: {f"0x{a:03X}": e for a, e in entries.items()}
                   for name, entries in findings.items()}, handle,
                  ensure_ascii=False, indent=2)

    print(f"\nwrote {report}")
    for name, entries in findings.items():
        addresses = ", ".join(f"0x{a:03X}" for a in sorted(entries)) or "none"
        print(f"  {name}: {addresses}")


if __name__ == "__main__":
    main()
