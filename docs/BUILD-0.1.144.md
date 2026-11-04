# MagicControl0.1.144 — owner-authorized GM5 common control integration

Owner requests22.10.2026: reuse GM6 mechanisms for GM5HG0A immediately, including
fans, battery logger, lighting and limits; CPU is AMD, GPU is NVIDIA in hybrid mode.

## Shared implementation

- Exact MECHREVO/Yilong15Pro Series GM5HG0A, BIOS N.1.09MRO19 and native installed
  UWACPIDriver11.2.18.347 SHA e9840521... are checked before access.
- GM5 joins the existing typed Gm6Controller, not legacy X5 MainWindow::ec_.
  Common DeviceOperation/EC mutex, journal RMW/readback/owned rollback preserved.
- Add optional identity probe mask (defaultFF, so all old probes keep their semantics).
  GM5 requires78E.6, stable nonzero/nonFF ProjectID, and a fixed live ProjectID
  within a controller lifetime. Unknown ProjectID is not replaced with GM6 ID25.
- Existing FAN Default/Manual RAM/online/Boost mechanisms shared. GPU RPM uses
  reviewed RamFan1.5 getter46B/46C. Factory-mailbox export remains unavailable
  for GM5 rather than passing GM6 project25 or replaying GM6 factory tables.
- AMD online temperature is PawnIO AMDFamily17, with fresh CPU+NVIDIA sensors;
  GM6 retains Intel DTS. Shared hybrid presentation continues to follow actual
  Windows display route between Radeon780M and RTX4060. No MUX switch added.
- AMD CPU-only783/784/785 requests use OEM SPL/SPPT/FPPT names and fresh firmware
  presets. Manual SPL/SPPT5..100W, FPPT5..200W, ordered SPL<=SPPT<=FPPT, Auto0/0/0. No FAN/mode writes
  from CPU-only commands. CPU-double flag retains conservative existing admission;
  unestablished scaled values are not presented as applied watts.
- Shared SingleZone RGB core/runtime capability, readback and lifecycle worker.
  GM5 device preferences and power draft use their own paths, not GM6 files.
- Shared XIF/XST decoder/EC logger and editor added for exact GM5; raw7D0 labelled
  DBD1 and removed from parsed charge-restart field. Windows original source retained.
- Native battery controller shares upper7B9, policy guards and RMW; GM5 percentage
  changes do not read/write7D0 as a lower threshold. Lower editor disabled for GM5.
  Existing GM6/HYDROC paired lower protocol unchanged. ROM policy remains explicit
  and owned, never NVM/Set_ROMID. Physical charge enforcement remains unmeasured.
- Windows generic AMD CPU/SMBus telemetry admitted for this exact board. Linux
  recognizes/presents the profile but gains no Windows EC writer.

## OEM evidence

Inventory of relevant local decompilations:44files/43distinct contents, saved in
research/gm5hg0a-20261022/ALL-LOCAL-OEM-VERSIONS.json, including DefaultTool,
GCUService reference, raw/readable recovered5.23.51.56, interface extracts.
Reference MyFanManager_RamFan1p5.ReCheckCpuPLMinimumValueA explicitly maps
AmdSPL->SetPL1Value(783), AmdSPPT->SetPL2Value(784), AmdFPPT->SetPL4Value(785).
MyFanCtrl selects RamFan1p5 by capability and commercial ProjectID branch;
GM5 runtime admission uses its own model/driver plus capability, not borrowed ID25.
GM5 DSDT defines INOU.ECRR and battery XIF/XST;7D0/7D1 are DBD1/DBD2.

## Verification scope

Common Windows/Linux build and portable hashes; no GUI or hardware tests, no
additional test suites. Existing model branch defaults reviewed in code; no claim
of physical regression testing. Owner must validate this first GM5 control build.

## Final portable verification

- windows:0.1.144 SHA256 e7eb39cc66f076e8bff9b4a7b6d047fe3938da281e1c3eca60aac06ef46bfb77
- linux:0.1.144 SHA256 e1deb4b9cc6a647b653fb7313954c729b6da8a314fb77b4549c7ac95f6da2658
