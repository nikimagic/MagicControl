MagicControl 0.1.105 — battery candidates and lower97 experiment

Common Windows/Linux Release completed and staged by Tools/Build-All.ps1.
Verification: verification-0.1.105.json (450 source inputs, four binaries,
six personal files, eight protected UI sources, eleven saved log files).
No application, hardware experiment or automated tests were launched.

Exact Wujie X5SP4NAG only: 61 raw EC fields and 13 named candidate bits.
Core data requires repeated identical samples; optional fields are checked
individually and are not an atomic EC snapshot. Unknown is never zero.
Log all observed candidate changes and full value snapshots on integer SOC
changes and every 10 seconds. The first snapshot includes field metadata.
SETs uses the shared battery table (131 rows), without changing FANs, KEYS,
fonts, controls or scrollbar mechanics. Other model register maps are unchanged.

User requested lower field 0x7D0=97 for all four Wujie upper buttons.
The OEM reference setter range is 1..95; 97 is explicitly experimental.
Its firmware role and enforcement remain unverified. Slow-charge threshold
is a hypothesis. Masked writes preserve bit7 and retain owned rollback.
Saved upper10 is preserved; the assistant did not apply the new setting.

Discharge evidence was copied, not moved, into the private portable folder:
MagicControl/windows/логи батареи/Wujie-2026-10-21-разряд-84-5
The full original session and filtered 84→5 / 70→5 intervals are retained.
The archive and runtime settings are intentionally excluded from Git.
