# MagicControl0.1.148 — MACHENIKE L16 profile, driver and support

- Add exact MACHENIKE/L16/L16 descriptor and separate diagnostic profile.
  Append menu index6 on Windows/Linux, preserving all existing indices/order.
  Reuse GPU preview/card components for AMD7945HX + RTX5070Ti; Radeon visibility
  follows the same actual-adapter/preview rules. No card/control geometry changes.
- INFO support resolves to official AMD7745HX/7945HX RTX50 catalogue342.
  Do not reuse Intel configuration344 or guess a BIOS updater.
- Found original ControlCenterX5.60.60.17: package111352434bytes, ZIP CRC verified;
  its native UWACPIDriver10.48.11.331 exactly matches embedded SYS/INF/CAT.
  Extend runtime provenance metadata, reuse existing embedded driver; no extra
  copy, automatic installer/service control or replacement of another driver.
- Explicit diagnostic EC dump extends the existing shared GM5 capture branch
  using exact L16 N.1.13MAC10 identity, installed PnP/service/path/SYS hash,
  read-only backend, bounded000–FFF and fill rejection. No new EC writers.
  Archive0.1.118 has no EC bytes; ProjectID/capability/table evidence is pending.
- Exact L16 CPU/DIMM monitoring uses existing AMD PawnIO/SMBus provider and its
  read checks. Current hardware readings still require owner feedback.
- Static OEM C#/IL/resources and originals are preserved under
  research/new-l16-20261022. GCUService protected stubs remain explicitly marked;
  DefaultTool is usable ordinary C#. No OEM program/driver executed.

Common Release Windows/Linux, stage both portable and verify hashes/versions.
No tests added/run or hardware commands issued. Existing model writer guards
and settings preserved; no GM5/GM6 fan/keyboard capability borrowed for L16.

Verified portable:
windows 0.1.148 SHA256 CC21BFD5852229CA5E3BF69E8AB70BEBD0AE16A1319E9BB11379A53FE5254027
linux 0.1.148 SHA256 919F8679A311786A738BA527500D023D7C31AFBD0A394FAC03FEF8D2A682A8AA
Static OEM export completed:2831 C# files/22 modules;160 assembly metadata records,231242 methods;126 explicit GCUBridge body-read errors;9052 resources,37 BAML,0 conversion errors;244PE/84native ASM. Report:research/new-l16-20261022/DECOMPILATION-REPORT.txt.

