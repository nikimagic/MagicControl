"""When does bit 2 of register 0x741 change, and what else changes with it.

The stored dumps show only three values for 0x741: 0x80, 0x81 and 0x84. Bit 7 is
constant, bit 0 matches the manual-control flag, and bit 2 appears only in the
later runs. This script finds the exact moment bit 2 appears and looks for any
other EC register that moves at the same time, which is how a meaning is
identified instead of guessed.

Reads only the stored dumps.

Usage:
    python tests/test_util/analysis/register_0741_events.py
"""
from pathlib import Path
import collections
import json

ROOT = Path(__file__).resolve().parents[3]
LOGS = ROOT / "tests/test_util/logs"


def main():
    for path in sorted(LOGS.rglob("dump.jsonl")):
        run = path.parent.name
        history = []
        with path.open(encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if '"event":"ec_dump"' not in line:
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                value = None
                snapshot = {}
                for byte in record.get("bytes", []):
                    address = byte.get("address")
                    raw = byte.get("raw")
                    if not isinstance(address, int) or not isinstance(raw, int):
                        continue
                    snapshot[address] = raw
                    if address == 0x741:
                        value = raw
                if value is not None:
                    history.append((record.get("sample"), value, snapshot))

        if not history:
            continue

        values = sorted({value for _, value, _ in history})
        if len(values) < 2:
            continue

        print(f"== {run}")
        print(f"   значений {len(history)}, уникальных {len(values)}: "
              f"{', '.join(f'0x{v:02X}' for v in values)}")

        # Report each change of 0x741, with the registers that moved on the same
        # sample. A register moving at the same instant is a candidate companion.
        previous = None
        for sample, value, snapshot in history:
            if previous is not None and value != previous[1]:
                changed = []
                for address, raw in snapshot.items():
                    before = previous[2].get(address)
                    if before is not None and before != raw:
                        changed.append((address, before, raw))
                print(f"   sample {previous[0]} -> {sample}: "
                      f"0x{previous[1]:02X} -> 0x{value:02X}; "
                      f"изменилось регистров: {len(changed)}")
                for address, before, raw in sorted(changed)[:24]:
                    print(f"      0x{address:03X}: 0x{before:02X} -> 0x{raw:02X}")
            previous = (sample, value, snapshot)

        # How long each state lasted.
        runs_of_state = []
        for _, value, _ in history:
            if runs_of_state and runs_of_state[-1][0] == value:
                runs_of_state[-1][1] += 1
            else:
                runs_of_state.append([value, 1])
        print("   длительности состояний: " +
              ", ".join(f"0x{v:02X}x{n}" for v, n in runs_of_state))
        print()


if __name__ == "__main__":
    main()
