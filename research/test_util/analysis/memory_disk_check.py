"""Cross-check of the memory and disk temperature channels.

Two questions decide whether these readings may be shown as separate sensors:

1. Do the two DDR5 SPD hubs report different values, or is one a copy of the
   other? A single real sensor duplicated under two addresses is not two sensors.
2. Do the NVMe sensor indices move independently, and what are their thresholds?

Reads only stored logs. No hardware access.

Usage:
    python research/test_util/analysis/memory_disk_check.py
"""
from pathlib import Path
import collections
import json
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
LOGS = ROOT / "out/test_util/logs"
OUT = ROOT / "out/test_util/generated/memory_disk_check"

# Channels to compare, by (source, id).
WATCH = {
    "DDR5 0x50": ("PawnIO SMBus", "smbus/port0/0x50/temperature"),
    "DDR5 0x51": ("PawnIO SMBus", "smbus/port0/0x51/temperature"),
    "SSD idx 0": ("Windows NVMe SMART", "PhysicalDrive0/temperature/0"),
    "SSD idx 1": ("Windows NVMe SMART", "PhysicalDrive0/temperature/1"),
    "SSD idx 2": ("Windows NVMe SMART", "PhysicalDrive0/temperature/2"),
    "SSD ctl 0": ("Windows Storage Temperature IOCTL", "PhysicalDrive0/property/51/sensor/0"),
    "SSD ctl 2": ("Windows Storage Temperature IOCTL", "PhysicalDrive0/property/51/sensor/2"),
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    values = collections.defaultdict(list)      # name -> [(run, value)]
    raw_payloads = collections.defaultdict(list)  # name -> [raw]

    for path in sorted(LOGS.glob("*windows*/dump.jsonl")):
        run = path.parent.name
        with path.open(encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if '"event":"sensor"' not in line:
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                key = (record.get("source"), record.get("id"))
                for name, wanted in WATCH.items():
                    if key == wanted and record.get("status") == "ok":
                        value = record.get("value")
                        if isinstance(value, (int, float)) and not isinstance(value, bool):
                            values[name].append((run, float(value)))
                        if record.get("raw") is not None:
                            raw_payloads[name].append(record.get("raw"))

    report = OUT / "report.md"
    with report.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Память и диск: проверка каналов\n\n")
        handle.write("| Канал | Снимков | Мин | Макс | Среднее | Уникальных значений |\n")
        handle.write("|---|---:|---:|---:|---:|---:|\n")
        for name in WATCH:
            rows = values.get(name, [])
            if not rows:
                handle.write(f"| {name} | 0 | — | — | — | — |\n")
                continue
            numbers = np.array([v for _, v in rows])
            handle.write(f"| {name} | {len(numbers)} | {numbers.min():.3f} | "
                         f"{numbers.max():.3f} | {numbers.mean():.3f} | "
                         f"{len(np.unique(numbers))} |\n")

        # Do the two memory channels differ from each other at the same moment?
        handle.write("\n## Различаются ли два канала памяти\n\n")
        pairs = collections.defaultdict(lambda: [None, None])
        for run, value in values.get("DDR5 0x50", []):
            pairs[run][0] = value if pairs[run][0] is None else pairs[run][0]
        # Compare sample by sample, run by run, by index rather than by time,
        # because both channels are read inside the same cycle.
        left = [v for _, v in values.get("DDR5 0x50", [])]
        right = [v for _, v in values.get("DDR5 0x51", [])]
        if len(left) == len(right) and left:
            a = np.array(left)
            b = np.array(right)
            difference = a - b
            handle.write(f"Одинаковых значений: {int((difference == 0).sum())} из {len(a)}.\n\n")
            handle.write(f"Разница 0x50 минус 0x51: мин {difference.min():+.3f}, "
                         f"макс {difference.max():+.3f}, среднее {difference.mean():+.3f} °C.\n\n")
            if np.all(difference == 0):
                handle.write("Каналы неразличимы: это может быть один датчик, "
                             "прочитанный дважды.\n")
            else:
                handle.write("Каналы различаются, значит это два независимых "
                             "показания, а не копия одного.\n")
        else:
            handle.write("Сравнить не удалось: разные количества снимков.\n")

        # Raw payloads, which name the device and the thresholds.
        handle.write("\n## Сырые данные датчиков\n\n")
        for name, payloads in raw_payloads.items():
            if not payloads:
                continue
            handle.write(f"### {name}\n\n```json\n")
            handle.write(json.dumps(payloads[0], ensure_ascii=False, indent=2)[:2000])
            handle.write("\n```\n\n")

    print(f"wrote {report}")
    for name in WATCH:
        rows = values.get(name, [])
        if rows:
            numbers = np.array([v for _, v in rows])
            print(f"  {name:12} n={len(numbers):3}  {numbers.min():.2f}..{numbers.max():.2f}  "
                  f"unique={len(np.unique(numbers))}")
        else:
            print(f"  {name:12} no data")


if __name__ == "__main__":
    main()
