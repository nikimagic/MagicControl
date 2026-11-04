# MagicControl0.1.145 — GM5 physical power-mode button

Owner clarifies: the separate physical button next to laptop power button,
already integrated on GM6. Exact supplied OEM MBB5.52.50.0 WMIEC maps event176
to MyFanCtrl.ModeSwitchChanged. RamFan1p5 has Office→Gaming→Turbo→Custom loop
when Turbo available; support from49F.bit1. Native mode751 values A0/00/10;
Custom light/flag handling matches existing shared codec.

- OemEventMonitor admits GM5 AcpiTest_EventULong through common exact profiles.
- Existing subscription callback queues APP741.0 arm, FIFO/epoch/owner gates,
  checked command result and common OSD/update; no new windows or UI geometry.
- Existing controller selects own GM5 OEM default tables from the supplied
  UserFanTables/DefaultFanTable_Office/Gaming/Turbo.json. Generated16-point data
  are separate model data; serializer/transaction/rollback remain shared.
- NoTurbo49F.1=0 uses Office/Gaming; Custom exits to Office. GM6 cycle unchanged.
- Custom keeps own GM5 saved power/curve intent and AMD effective FPPT Double
  /2 encoding/readback. Reject odd effective watts before applying.
- No GPU/MUX/UEFI/battery writes. Existing AC requirement for nonzero Custom CPU
  limits is retained. Default/Manual/Max button semantics unchanged.

Shared Windows/Linux build, stage portable, version/SHA256 comparison. No new
application tests or physical/hardware execution. GM5 owner feedback still needed.
OEM source and table hashes: research/gm5hg0a-20261022/MODE-BUTTON-TABLES.json.

Verified staged portable SHA256:
MagicControl/windows/MagicControl.exe: 6C2C6034F316B6068B65B0D14D46328E3ECA256CD578BE36FB43E98C6BF65858
MagicControl/linux/MagicControl: 2F3ECFB6B712C23B98CCAB128C3056CBE2AF9234FD2F5973E5F3B18DF6228F31
