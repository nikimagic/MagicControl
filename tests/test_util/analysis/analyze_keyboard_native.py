"""Compare native-mode EC observations with the saved post-boot baseline."""
import csv
import datetime
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = Path(__file__).resolve().parent / "keyboard-ec-timeout"
BASELINE = Path("C:/Users/admin/Documents/mechrevo/Fan_X15Pro/Diagnostics/EcSnapshots/default_EC/20261005-162655/ec.bin")
TRACKED = (0x43E, 0x726, 0x727, 0x734, 0x735, 0x736, 0x741, 0x751,
           0x767, 0x769, 0x76A, 0x76B, 0x783, 0x784, 0x785, 0x78C,
           0x78D, 0x7C5, 0x7C6, 0x7CC, 0x7CD, 0x7CE)


def read_run(path):
    contexts, frames = {}, []
    completed = False
    with path.open(encoding="utf-8-sig") as source:
        for line in source:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue  # An active collector may have a partial final line.
            if row.get("event") == "observation_context":
                contexts[row["sample"]] = row
            elif row.get("event") == "ec_dump":
                frames.append(row)
            elif row.get("event") == "run_end":
                completed = row.get("completed", False)
    return contexts, frames, completed


def main():
    baseline = BASELINE.read_bytes()
    summaries, timeline, differences = [], [], []
    for path in sorted((ROOT / "logs" / "keyboard-native-series").glob("*/dump.jsonl")):
        contexts, frames, completed = read_run(path)
        values = [{byte["address"]: byte.get("raw") for byte in frame["bytes"]}
                  for frame in frames]
        if not values:
            continue
        transitions, power_changes = [], []
        previous = None
        previous_power = None
        for frame, raw in zip(frames, values):
            context = contexts.get(frame["sample"], {})
            keyboard = context.get("keyboard", {})
            policy = context.get("windows_policy", {})
            bit2 = bool(raw.get(0x741, 0) & 4)
            point = {"run": path.parent.name, "sample": frame["sample"], "utc": frame["utc"],
                     "keyboard_idle_ms": keyboard.get("idle_ms"),
                     "keyboard_header_events": keyboard.get("header_events"),
                     "windows_input_idle_ms": context.get("idle_ms"),
                     "ac_line_status": context.get("ac_line_status"),
                     "windows_effective_mode": policy.get("effective_mode"),
                     "EC_0x741_bit2": int(bit2)}
            point.update({f"EC_0x{address:03X}": raw.get(address) for address in TRACKED})
            timeline.append(point)
            if previous is None or bit2 != previous:
                transitions.append({key: point[key] for key in
                                    ("sample", "utc", "EC_0x741_bit2", "keyboard_idle_ms",
                                     "keyboard_header_events", "windows_input_idle_ms")})
            previous = bit2
            state = (context.get("ac_line_status"), policy.get("effective_mode"), raw.get(0x751))
            if state != previous_power:
                power_changes.append({"sample": frame["sample"], "utc": frame["utc"],
                                      "ac_line_status": state[0], "windows_effective_mode": state[1],
                                      "EC_0x751": state[2], "policy": policy})
            previous_power = state
        for address in range(min(4096, len(baseline))):
            observed = sorted({raw.get(address) for raw in values if raw.get(address) is not None})
            if observed != [baseline[address]]:
                differences.append({"run": path.parent.name, "address": f"0x{address:03X}",
                                    "baseline_decimal": baseline[address],
                                    "observed_values": " ".join(map(str, observed)),
                                    "distinct_values": len(observed)})
        summaries.append({"run": path.parent.name, "completed": completed,
                          "frames": len(frames), "first_utc": frames[0]["utc"], "last_utc": frames[-1]["utc"],
                          "failed_bytes": sum(frame["failed_bytes"] for frame in frames),
                          "tracked_registers": {f"0x{address:03X}": sorted({raw.get(address) for raw in values})
                                                for address in TRACKED},
                          "bit2_transitions": transitions, "power_transitions": power_changes})
    OUTPUT.mkdir(exist_ok=True)
    summary = {"baseline": str(BASELINE), "baseline_sha256": hashlib.sha256(baseline).hexdigest(),
               "read_only": True, "runs": summaries,
               "interpretation": "0x741 bit2 is correlated with native backlight blanking. It is not a confirmed writable timeout setting.",
               "writable_timeout_register": None}
    (OUTPUT / "native-summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for filename, rows in (("native-timeline.csv", timeline), ("stock-differences.csv", differences)):
        if not rows:
            continue
        with (OUTPUT / filename).open("w", encoding="utf-8-sig", newline="") as destination:
            writer = csv.DictWriter(destination, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    print(json.dumps([{"run": run["run"], "frames": run["frames"], "completed": run["completed"],
                       "failed_bytes": run["failed_bytes"], "power_transitions": len(run["power_transitions"])}
                      for run in summaries], ensure_ascii=False))


if __name__ == "__main__":
    main()
