"""Inventory of every sensor channel present in the captured logs.

Reads only the stored dump.jsonl files. No hardware access, nothing is written
back into the raw logs.

Goal: before hunting for a specific temperature, establish which channels were
actually captured on this machine, on which operating system, with what units and
what range. A channel that was never captured cannot be confirmed from these
logs, and saying so is more useful than guessing from a plausible number.

Usage:
    python research/test_util/analysis/sensor_inventory.py
"""
from pathlib import Path
import collections
import json
import os

ROOT = Path(__file__).resolve().parents[3]
LOGS = ROOT / "out/test_util/logs"
OUT = ROOT / "out/test_util/generated/sensor_inventory"

# A sensor is interesting for temperature work if it declares it, or if its id or
# name hints at it. Everything is still listed; this only drives the grouping.
TEMPERATURE_HINTS = ("temp", "thermal", "tctl", "tdie", "gfx", "soc", "ddr",
                     "dimm", "spd", "ssd", "nvme", "npu", "ipu", "hotspot", "edge")


def looks_temperature(source, identifier, name):
    text = f"{source} {identifier} {name}".lower()
    return any(hint in text for hint in TEMPERATURE_HINTS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    # (os, source, id, name, unit, status) -> list of values
    channels = collections.defaultdict(list)
    files = sorted(LOGS.rglob("dump.jsonl"))
    print(f"scanning {len(files)} dump files")

    for path in files:
        # The run directory is named <model>_<os>_<utc>; the OS is the segment
        # before the trailing timestamp.
        parts = path.parent.name.split("_")
        platform = parts[1] if len(parts) > 2 else "unknown"
        events = collections.Counter()
        with path.open(encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if '"event":"sensor"' not in line:
                    if '"event":"ec_dump"' in line:
                        events["ec_dump"] += 1
                    elif '"event":"run_start"' in line:
                        events["run_start"] += 1
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                events["sensor"] += 1
                value = record.get("value")
                if not isinstance(value, (int, float)) or isinstance(value, bool):
                    continue
                key = (
                    platform,
                    record.get("source", "?"),
                    record.get("id", "?"),
                    record.get("name", ""),
                    record.get("unit", ""),
                    record.get("status", ""),
                )
                channels[key].append(float(value))
        print(f"  {path.parent.name}: {dict(events)}")

    # A compact table, ordered so the temperature-looking channels come first.
    rows = []
    for (platform, source, identifier, name, unit, status), values in channels.items():
        rows.append({
            "platform": platform,
            "source": source,
            "id": identifier,
            "name": name,
            "unit": unit,
            "status": status,
            "samples": len(values),
            "min": min(values),
            "max": max(values),
            "temperature_like": looks_temperature(source, identifier, name),
        })

    rows.sort(key=lambda r: (not r["temperature_like"], r["source"], r["id"]))

    table = OUT / "channels.tsv"
    with table.open("w", encoding="utf-8", newline="") as handle:
        handle.write("platform\tsource\tid\tname\tunit\tstatus\tsamples\tmin\tmax\ttemperature_like\n")
        for row in rows:
            handle.write(
                f"{row['platform']}\t{row['source']}\t{row['id']}\t{row['name']}\t"
                f"{row['unit']}\t{row['status']}\t{row['samples']}\t"
                f"{row['min']:.4f}\t{row['max']:.4f}\t{int(row['temperature_like'])}\n")

    # Report only the temperature-looking ones with a usable status, because that
    # is the short list worth reading.
    report = OUT / "report.md"
    with report.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Какие датчики вообще есть в логах\n\n")
        handle.write(f"Просканировано файлов: {len(files)}.\n\n")
        usable = [r for r in rows if r["temperature_like"] and r["status"] == "ok"]
        handle.write(f"Каналов всего: {len(rows)}. Из них похожих на температуру "
                     f"и со статусом ok: {len(usable)}.\n\n")
        handle.write("| ОС | Источник | id | Имя | Ед. | Снимков | Мин | Макс |\n")
        handle.write("|---|---|---|---|---|---:|---:|---:|\n")
        for row in usable:
            handle.write(f"| {row['platform']} | {row['source']} | {row['id']} | {row['name']} | "
                         f"{row['unit']} | {row['samples']} | {row['min']:.3f} | {row['max']:.3f} |\n")

        # Channels that exist but are not ok, which explains absences.
        bad = [r for r in rows if r["temperature_like"] and r["status"] != "ok"]
        if bad:
            handle.write("\n## Похожи на температуру, но статус не ok\n\n")
            handle.write("| ОС | Источник | id | Имя | Статус | Снимков |\n")
            handle.write("|---|---|---|---|---|---:|\n")
            for row in bad:
                handle.write(f"| {row['platform']} | {row['source']} | {row['id']} | {row['name']} | "
                             f"{row['status']} | {row['samples']} |\n")

    print(f"\nwrote {table}")
    print(f"wrote {report}")
    print(f"channels: {len(rows)}, temperature-like and ok: "
          f"{sum(1 for r in rows if r['temperature_like'] and r['status'] == 'ok')}")


if __name__ == "__main__":
    main()
