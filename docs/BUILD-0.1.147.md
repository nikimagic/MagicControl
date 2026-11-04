# MagicControl0.1.147 — GM5 memory and battery temperatures

GM5 FANs showed no DIMM/battery temperatures in logs_GM5HG0A(3), build0.1.144.

- MonitoringWindows admitted GM5 SMBus but left the CPU gate false when the
  legacy X5 EcAccess was absent. PawnTelemetryReader::read returned before
  reaching SMBus. Admit the exact GM5 profile to both existing CPU/SMBus paths.
  This also restores its existing package/core MSR telemetry path. Keep the
  existing signed modules, PCI/SMBus mutexes, identity/port0/SPD5118 checks,
  valid-range decoding and backoff. No guessed DIMM temperature is inserted.
- Supplied OEM MBB5.52.50.0 GCUService/DefaultTool Define.ECSpec names
  ecBt1Temperature=1186(0x4A2). Fresh dump word0x0C1C=3100 is consistent with
  SBS tenths kelvin36.85C. Physical calibration remains unverified.
  Read this word only for GM5, after exact driver/board admission and battery
  BPST/BSOK. Require two matching words, use existing decoder/range check.
  Keep independent core battery fields if temperature fails. Publish raw and
  source/status in EC details; supplement a single physical OS battery when
  OS temperature is missing, preserving its telemetry timestamp.
- FANs uses existing shared snapshot/cards; no UI geometry, cooling commands,
  battery limits, EC writers or other model temperature maps changed.

Evidence: research/gm5hg0a-logs3-20261022; OEM under
research/devices/gm5hg0a/oem-5.52.49.6/laptop-control/csharp/GCUService/Define.ECSpec.cs.

Common Release Windows/Linux build and staged portable hash/version comparison.
No tests added/run, no hardware commands or application execution.
DIMM hardware response must be checked on GM5 after this provider admission fix.

Verified staged portable:
windows 0.1.147 SHA256 7F38EF59A974AA502E45A6D0C2C11FD32EC9DCE04161C20F5E67A7A71F59AB6A
linux 0.1.147 SHA256 43359CC693ABCFE6B45BC89A9659CFBBA44B888A2D7B3A87E1700EFD44561C1B
Second fresh archive logs_GM5HG0A(4), still0.1.146:4A2=0x0BE0=3040 ->30.85C. It independently preserves a plausible SBS temperature word while OS BatteryTemperature remains unsupported.

