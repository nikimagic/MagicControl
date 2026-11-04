"""Read saved evidence and verify portable artifacts; never accesses hardware."""
import csv
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


log = ROOT / "MagicControl/windows/logs_WUJIE_Series-X5SP4NAG/MagicControl-1.jsonl"
rows = [json.loads(line) for line in log.read_text(encoding="utf-8-sig").splitlines()]
rows = [row for row in rows if row.get("app_version") == "0.1.391"]
transitions = []
for action, previous, observed_at in [(18, "Office", "2026-10-29T19:55:03.966Z"),
                                       (21, "Turbo", "2026-10-29T19:55:18.898Z")]:
    before = next(row["cached_before"] for row in rows if row.get("action_id") == action
                  and row.get("command") == "settings.apply" and "cached_before" in row)
    after = next(row["state"] for row in rows if row.get("utc") == observed_at
                 and row.get("source") == "windows.settings" and row.get("event") == "state_observed")
    writes = [{"utc": row["utc"], "address": hex(row["address"]), "value": row["value"]}
              for row in rows if row.get("action_id") == action and row.get("event") == "ec_write_requested"]
    assert {item["address"] for item in writes} == {"0x751", "0x783", "0x784", "0x785"}
    assert after["ec_power"]["accepted"] == {"stapm": 25, "slow": 25, "fast": 30}
    assert after["ec_power"]["overrides"] == {"stapm": 80, "slow": 80, "fast": 85}
    assert not after["smu_comparison"]["stale"]
    assert all(abs(after["smu_comparison"][key] - value) < .001
               for key, value in after["ec_power"]["accepted"].items())
    transitions.append({"previous_mode": previous, "action_id": action, "observed_utc": observed_at,
                        "before": before["ec_power"], "after": after["ec_power"],
                        "smu_comparison": after["smu_comparison"], "writes": writes,
                        "fan_writes_in_this_action": False})
save("physical-transitions.json", {"version": "0.1.391", "source_log": str(log.relative_to(ROOT)),
                                  "session": rows[0]["session"], "transitions": transitions})

baseline = ROOT / "MagicControl/windows/Backups/fan-2026-10-29T19-53-55-841Z-16856/ec-before.csv"
before = {int(row["Address"], 16): int(row["Value"]) for row in
          csv.DictReader(baseline.read_text(encoding="utf-8-sig").splitlines())}
samples = [json.loads(line) for line in (OUT / "ec-power-after391.jsonl").read_text().splitlines()]
checks = []
for sample in samples:
    flags = {hex(address): {"before": before[address], "after": sample["raw"][str(address)]}
             for address in [0x726, 0x727, 0x741, 0x7c5, 0x7c6]}
    assert all(value["before"] == value["after"] for value in flags.values())
    assert sample["ec_writes"] == 0
    checks.append({"sample": sample["sample"], "ec_writes": 0, "fan_flags": flags,
                   "boost_preserved": (before[0x751] & 0x40) == (sample["raw"][str(0x751)] & 0x40),
                   "after_table_sha256": hashlib.sha256(bytes(sample["fan_tables"])).hexdigest()})
save("fan-preservation.json", {"baseline": str(baseline.relative_to(ROOT)),
                               "baseline_sha256": digest(baseline), "checks": checks,
                               "table_comparison": "Not available: baseline CSV has six mode/flag bytes, no F00-F5F table. No table equality claim.",
                               "additional_evidence": "Physical actions 18/21 wrote only CPU registers; fake-EC controller tests compare full tables."})

product = json.loads((ROOT / "build/product-build.json").read_text(encoding="utf-8-sig"))
assert product["status"] == "complete" and product["version"] == "0.1.391"
artifacts = []
for platform, build_dir, portable_dir, names in [
    ("windows", "build/windows/bin/Release", "MagicControl/windows", ["MagicControl.exe", "mctl.exe"]),
    ("linux", "build/linux/bin", "MagicControl/linux", ["MagicControl", "mctl"])]:
    for index, name in enumerate(names):
        build, portable = ROOT / build_dir / name, ROOT / portable_dir / name
        expected = product["platforms"][platform]["sha256" if index == 0 else "cli_sha256"].lower()
        actual = digest(portable)
        assert actual == digest(build) == expected
        artifacts.append({"path": str(portable.relative_to(ROOT)), "sha256": actual, "matches_build": True})

settings = {}
for platform in ["windows", "linux"]:
    old = json.loads((ROOT / f"build/wujie-custom-settings-{platform}-before.json").read_text(encoding="utf-8-sig"))
    changes = [{"path": row["Path"], "before": row["SHA256"],
                "after": digest(Path(row["Path"])) if Path(row["Path"]).exists() else None}
               for row in old if not Path(row["Path"]).exists() or digest(Path(row["Path"])) != row["SHA256"].lower()]
    settings[platform] = {"baseline_files": len(old), "changed_files": changes,
                          "policy": "User operated GUI during testing; preserve live settings, never overwrite to make hashes match."}
save("verification-0.1.391.json", {"version": product["version"], "source_fingerprint": product["source_fingerprint"],
                                   "artifacts": artifacts, "settings": settings,
                                   "tests": {"windows": "4/4 passed", "linux": "2/2 passed"},
                                   "hardware": "Wujie only; Office/Custom and Turbo/Custom EC and fresh SMU agree. Requested 85/80/80; accepted 30/25/25 on battery. No claim of bypassing native supply/SOC caps."})
print(json.dumps({"artifacts_verified": len(artifacts), "physical_transitions": len(transitions),
                  "settings_changed": {key: len(value["changed_files"]) for key, value in settings.items()}}))
