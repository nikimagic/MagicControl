"""Summarize completed interactive 0x741 experiments; reads saved files only."""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(__file__).resolve().parent / "keyboard-ec-timeout"


def main():
    results = []
    pending = []
    tested_union = 0
    for source in sorted((ROOT / "logs" / "ec-bit-test").glob("*/events.jsonl")):
        records = []
        for line in source.read_text(encoding="utf-8-sig").splitlines():
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                continue
        if (source.parent / "restore.txt").exists():
            pending.append(str(source.parent))
        for bit in range(8):
            phase = f"bit-{bit}"
            rows = [row for row in records if row.get("phase") == phase]
            writes = [row for row in rows if row.get("event") == "write_requested"]
            if not writes:
                continue
            returned = [row for row in rows if row.get("event") == "write_returned"]
            restored = [row for row in rows if row.get("event") == "restore_result"]
            samples = [row for row in rows if row.get("event") == "sample"]
            flags = sorted({row["controls"]["0x741"] for row in samples})
            changes = []
            previous = None
            for row in samples:
                value = row["controls"]["0x741"]
                if value != previous:
                    changes.append({"utc": row["utc"], "value": value,
                                    "windows_idle_ms": row.get("windows_idle_ms")})
                previous = value
            result = {"session": source.parent.name, "bit": bit, "mask": 1 << bit,
                      "write_utc": writes[-1]["utc"], "before": writes[-1]["before"],
                      "requested": writes[-1]["wanted"],
                      "immediate_readback": returned[-1]["readback"] if returned else None,
                      "samples": len(samples), "observed_0x741": flags,
                      "first_sample_utc": samples[0]["utc"] if samples else None,
                      "first_sample_0x741": samples[0]["controls"]["0x741"] if samples else None,
                      "transitions": changes,
                      "other_controls": {key: sorted({row["controls"][key] for row in samples})
                                         for key in ("0x726", "0x727", "0x751", "0x767", "0x78C", "0x7C5", "0x7C6")},
                      "restore_confirmed": bool(restored and restored[-1]["confirmed"]),
                      "restore_utc": restored[-1]["utc"] if restored else None}
            if result["restore_confirmed"]:
                tested_union |= 1 << bit
            results.append(result)
    summary = {"address": "0x741", "hardware_access": False, "tested_bits_union": tested_union,
               "all_eight_tested_and_restored": tested_union == 255 and not pending,
               "pending_recovery_sessions": pending, "results": results}
    OUT.mkdir(exist_ok=True)
    (OUT / "bit-tests.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with (OUT / "bit-tests.csv").open("w", encoding="utf-8-sig", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=["session", "bit", "mask", "before", "requested",
                                                  "immediate_readback", "samples", "first_sample_0x741",
                                                  "restore_confirmed", "write_utc", "restore_utc"])
        writer.writeheader()
        for result in results:
            writer.writerow({key: result[key] for key in writer.fieldnames})
    print(json.dumps({key: value for key, value in summary.items() if key != "results"}))


if __name__ == "__main__":
    main()
