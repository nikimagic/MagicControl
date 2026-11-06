"""What register 0x741 actually does in the captured EC dumps.

Answers three questions from the stored logs alone:

1. Which bits are ever set, and how often? A bit that never moves is not
   evidence of anything; a bit that tracks a known state is.
2. Does the value differ between runs, which would mean it carries session
   state rather than a constant.
3. Does any bit correlate in time with a known signal (CPU temperature, fan
   revolution, AC/battery, keyboard events), which is how a meaning is proven
   rather than guessed.

Reads only the stored dumps. No hardware access.

Usage:
    python research/test_util/analysis/register_0741.py
"""
from pathlib import Path
from datetime import datetime
import collections
import json

ROOT = Path(__file__).resolve().parents[3]
LOGS = ROOT / "out/test_util/logs"
OUT = ROOT / "out/test_util/generated/register_0741"
ADDRESS = 0x741

# Bit names as known from the Linux kernel driver for this exact board
# (drivers/platform/x86/uniwill/uniwill-acpi.c, EC_ADDR_AP_OEM 0x0741).
KERNEL_BITS = {
    0: "ENABLE_MANUAL_CTRL",
    3: "ITE_KBD_EFFECT_REACTIVE",
    5: "FAN_ABNORMAL",
}


def timestamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    observations = []   # (run, sample, utc, value)
    for path in sorted(LOGS.rglob("dump.jsonl")):
        parts = path.parent.name.split("_")
        platform = parts[1] if len(parts) > 2 else "unknown"
        with path.open(encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if '"event":"ec_dump"' not in line:
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                for byte in record.get("bytes", []):
                    if byte.get("address") == ADDRESS and isinstance(byte.get("raw"), int):
                        observations.append({
                            "run": path.parent.name,
                            "platform": platform,
                            "sample": record.get("sample"),
                            "utc": byte.get("utc"),
                            "value": byte["raw"],
                        })

    report = OUT / "report.md"
    with report.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Регистр 0x741: что показывают дампы\n\n")
        handle.write(f"Прочитано значений: {len(observations)}.\n\n")

        if not observations:
            handle.write("Значений не найдено.\n")
            print("no observations")
            return

        # --- Which bits are set, and how often -----------------------------
        bit_counts = collections.Counter()
        for item in observations:
            for bit in range(8):
                if item["value"] & (1 << bit):
                    bit_counts[bit] += 1

        handle.write("## Состояние битов\n\n")
        handle.write("| Бит | Маска | Установлен | Доля | Имя из ядра Linux |\n")
        handle.write("|---:|---|---:|---:|---|\n")
        for bit in range(8):
            count = bit_counts.get(bit, 0)
            share = count / len(observations)
            name = KERNEL_BITS.get(bit, "— не определено")
            handle.write(f"| .{bit} | 0x{1 << bit:02X} | {count} | {share * 100:.1f}% | {name} |\n")

        # --- Distinct values ----------------------------------------------
        values = collections.Counter(item["value"] for item in observations)
        handle.write("\n## Наблюдённые значения целиком\n\n")
        handle.write("| Значение | Двоичное | Раз | Биты |\n")
        handle.write("|---:|---|---:|---|\n")
        for value, count in sorted(values.items()):
            bits = " ".join(f".{bit}" for bit in range(8) if value & (1 << bit)) or "нет"
            handle.write(f"| 0x{value:02X} | {value:08b} | {count} | {bits} |\n")

        # --- Per run, to expose session state ------------------------------
        handle.write("\n## По запускам\n\n")
        handle.write("| Запуск | ОС | Значений | Уникальных | Значения |\n")
        handle.write("|---|---|---:|---:|---|\n")
        per_run = collections.defaultdict(list)
        for item in observations:
            per_run[(item["run"], item["platform"])].append(item["value"])
        for (run, platform), items in sorted(per_run.items()):
            unique = sorted(set(items))
            shown = ", ".join(f"0x{v:02X}" for v in unique[:8])
            handle.write(f"| {run} | {platform} | {len(items)} | {len(unique)} | {shown} |\n")

        # --- Does the value change inside a run? ---------------------------
        handle.write("\n## Изменяется ли значение внутри запуска\n\n")
        for (run, platform), items in sorted(per_run.items()):
            if len(set(items)) > 1:
                handle.write(f"- **{run}**: значение менялось, {len(set(items))} разных.\n")
        if all(len(set(items)) == 1 for items in per_run.values()):
            handle.write("Во всех запусках значение постоянно, внутри запуска не менялось.\n")

        # --- Does bit 7 separate runs? --------------------------------------
        handle.write("\n## Бит .7 отдельно\n\n")
        for (run, platform), items in sorted(per_run.items()):
            set_count = sum(1 for v in items if v & 0x80)
            handle.write(f"- {run}: .7 установлен в {set_count} из {len(items)}.\n")

    print(f"wrote {report}")
    print(f"observations: {len(observations)}")
    for bit in range(8):
        count = bit_counts.get(bit, 0)
        if count:
            print(f"  bit {bit} (0x{1 << bit:02X}): set in {count}/{len(observations)}"
                  f"  [{KERNEL_BITS.get(bit, 'unknown')}]")
    print("  distinct values:", ", ".join(f"0x{v:02X}" for v in sorted(values)))


if __name__ == "__main__":
    main()
