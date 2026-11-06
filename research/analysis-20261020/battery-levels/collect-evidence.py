"""Offline battery review: read existing OEM exports/resources, never run OEM code.

Requires the local research caches named below. GM6 resources.pri.xml was made
by Windows SDK makepri dump from resources.pri in the original 5.52.50.23 bundle.
HYDROC C# is the independently recovered exact 5.23.51.56 service, not GM6 code.
No driver, GUI, EC, firmware, registry or network operations are performed.
"""
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
GM = ROOT / "../Decompile/GM6IR0C/Control_Center"
HY = ROOT / "research/devices/hydroc-16-g2/oem-5.23.51.56/recovered-20261019"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(path):
    return path.relative_to(ROOT).as_posix()


def resources(path):
    keys = {
        "strBatteryMode", "strBatteryModeInfo", "strBatteryPerformance",
        "strBatteryPerformanceInfo", "strBatteryBalanced", "strBatteryBalancedInfo",
        "strBatteryHealthy", "strBatteryHealthyInfo",
    }
    rows, seen = [], set()
    for node in ET.parse(path).getroot().iter("NamedResource"):
        name, uri = node.get("name", ""), node.get("uri", "")
        if name not in keys:
            continue
        locale = uri.split("/")[-2]
        if locale not in {"en-us", "ru-ru", "zh-cn"}:
            continue
        for candidate in node.findall("Candidate"):
            value = candidate.findtext("Value", "")
            if (name, locale, value) not in seen:
                seen.add((name, locale, value))
                rows.append({"name": name, "locale": locale, "value": value})
    return {"source": relative(path), "sha256": digest(path), "strings": rows}


def member_locations(path, names):
    rows = []
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        for name in names:
            if re.search(r"\b" + re.escape(name) + r"\s*\(", line):
                rows.append({"name": name, "line": number, "declaration": bool(
                    re.search(r"\b(?:public|private|protected|internal)\b", line))})
    return {"path": relative(path), "sha256": digest(path), "locations": rows}


names = ["SetHealthProtectionHigh", "SetHealthProtectionMiddle", "SetHealthProtectionLow",
         "SetBatteryChargingLimit_Up", "SetBatteryChargingLimit_Down", "LoadBatteryLimitationDefault"]
gm_class = GM / "020001C6-GCUService.MySystem.BatteryProtection2.cs"
hy_class = HY / "readable-csharp/020001D1-GCUService.MySystem.BatteryProtection2.cs"
gm_hits = {name: [] for name in names[3:]}
for source in sorted(GM.glob("*.cs")):
    for number, line in enumerate(source.read_text(encoding="utf-8-sig").splitlines(), 1):
        for name in gm_hits:
            if re.search(r"\b" + name + r"\b", line):
                gm_hits[name].append({"path": relative(source), "line": number,
                    "text": line.strip()})

audit_path = HY / "audit.json"
audit = json.loads(audit_path.read_text(encoding="utf-8-sig"))
ui = {
    "gm6": resources(ROOT / "build/research-gm6ir0c/battery-ui-resources/resources.pri.xml"),
    "hydroc": resources(ROOT / "build/research-hydroc16g2/battery-ui-resources/GamingCenter3_Cross.UWP_5.23.51.56_x64-resources.pri.xml"),
}
pins = [
    ROOT / "build/research-gm6ir0c/samples/GCUService-active.exe",
    ROOT / "build/research-gm6ir0c/battery-ui-resources/resources.pri",
    ROOT / "build/research-gm6ir0c/battery-ui-resources/AppxManifest.xml",
    ROOT / "build/research-hydroc16g2/installer/app/AiStoneService/MyControlCenter/GCUService.exe",
    ROOT / "build/research-hydroc16g2/battery-ui-resources/GamingCenter3_Cross.UWP_5.23.51.56_x64-resources.pri",
]
evidence = {
    "date": "2026-10-20", "method": "static offline source/resource inspection",
    "target_executed": False, "hardware_accessed": False, "tests_executed": False,
    "oem_files": [{"path": relative(p), "bytes": p.stat().st_size, "sha256": digest(p)} for p in pins],
    "members": [member_locations(gm_class, names), member_locations(hy_class, names)],
    "gm6_percentage_method_name_occurrences": gm_hits,
    "gm6_search_scope": "complete local all-active C# export; name references, not a new IL call graph",
    "hydroc_exact_il_call_graph_source": relative(audit_path),
    "hydroc_exact_il_call_graph_sha256": digest(audit_path),
    "hydroc_exact_il_percentage_calls": {token: entry for token, entry in audit["battery_call_graph"].items()
        if entry["name"] in names[3:]},
    "oem_ui_mode_commands": [
        {"command": "PERFORMANCEDMODE", "registry_mode": 0, "mask": "0x30", "bits": "0x00", "label": "High Capacity Mode"},
        {"command": "BALANCEDMODE", "registry_mode": 1, "mask": "0x30", "bits": "0x10", "label": "Balanced Mode"},
        {"command": "HEALTHYMODE", "registry_mode": 2, "mask": "0x30", "bits": "0x20", "label": "Stationary Mode"},
    ],
    "profile_to_100_95_85_mapping_proven": False,
    "physical_voltage_or_current_cutoffs_measured": False,
    "reference_only_other_oem": "XMG explains hidden capacity reduction at displayed 100%; not a GM6/HYDROC calibration",
    "primary_web_sources": [
        "https://help.xmg.gg/hc/en-gb/articles/17824821322653-How-can-I-give-my-battery-the-best-health-and-longest-life-possible",
        "https://download.schenker-tech.de/temp/Quick%20Start%20Guide%20Control%20Center%20for%20XMG-2021_v1.2_en.pdf",
    ],
}
(OUT / "oem-ui-strings.json").write_text(json.dumps(ui, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
(OUT / "evidence.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("Saved offline evidence; no OEM code or hardware operations executed.")
