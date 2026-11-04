"""Inspect saved EC RAM snapshots. Does not access or write hardware."""
import collections
import datetime
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = Path(__file__).resolve().parent / "keyboard-ec-timeout"


def main():
    runs = []
    for path in sorted((ROOT / "logs").glob("*/dump.jsonl")):
        frames = []
        with path.open(encoding="utf-8-sig") as source:
            for line in source:
                row = json.loads(line)
                if row.get("event") != "ec_dump":
                    continue
                timestamp = datetime.datetime.fromisoformat(row["utc"].replace("Z", "+00:00"))
                values = {byte["address"]: byte["raw"] for byte in row["bytes"]
                          if isinstance(byte.get("raw"), int) and not byte.get("error")}
                frames.append((timestamp, values))
        if not frames:
            continue
        candidates = []
        for address in range(4096):
            values = [frame.get(address) for _, frame in frames]
            if any(value is None for value in values):
                continue
            hits = collections.Counter()
            changes = 0
            for (before, _), (after, _), old, new in zip(frames, frames[1:], values, values[1:]):
                delta = new - old
                changes += delta != 0
                seconds = (after - before).total_seconds()
                for direction in (1, -1):
                    hits[direction] += abs(delta - direction * seconds) <= 1
            matches = max(hits.values(), default=0)
            if matches >= 4 and matches >= changes / 2:
                candidates.append({"address": f"0x{address:03X}", "matches": matches,
                                   "changes": changes, "values": values,
                                   "meaning": "unconfirmed; not a writable timeout setting"})
        controls = {f"0x{address:03X}": sorted({frame.get(address) for _, frame in frames})
                    for address in (0x726, 0x727, 0x741, 0x767, 0x78C, 0x78D, 0x7C6)}
        runs.append({"run": path.parent.name, "snapshots": len(frames),
                     "first_utc": frames[0][0].isoformat(), "last_utc": frames[-1][0].isoformat(),
                     "controls": controls, "counter_candidates": candidates})
    summary = {"source": "saved 4 KiB EC RAM snapshots; not EC executable firmware",
               "hardware_access": False, "settings_writes": False,
               "snapshots": sum(run["snapshots"] for run in runs), "runs": runs,
               "writable_keyboard_timeout_register": None,
               "conclusion": "No keyboard idle/off/on markers or native-mode capture establish a timeout register."}
    OUTPUT.mkdir(exist_ok=True)
    (OUTPUT / "saved-snapshots.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{len(runs)} runs, {summary['snapshots']} snapshots; timeout register unconfirmed.")


if __name__ == "__main__":
    main()
