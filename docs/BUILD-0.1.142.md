# MagicControl0.1.142 — GM5HG0A integration stage

- Exact MECHREVO / GM5HG0A / Yilong15Pro Series identity, BIOS N.1.09MRO19.
- Common Windows/Linux menu appends stable model index5; existing indices unchanged.
- Shared preview uses AMD Ryzen7 8845H + NVIDIA RTX4060; existing card renderers,
  geometry, controls, embedded fonts, and actual-SMBIOS hardware dispatch retained.
- Windows explicit diagnostic RAM reader only: reviewed DSDT INOU.ECRR byte mapping
  FED50000+address, window0000–0FFF, existing read-only UWACPIDriver backend.
- Read-only PnP/service/Windows-path/SHA256 evidence reused with HYDROC through
  ReviewedDriverWindows.h. HYDROC identity, driver pin and operations unchanged;
  shared helper error messages now say Uniwill instead of HYDROC.
- GM5 driver pin:6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235.
  Unknown driver/BIOS refuses EC opening and retains diagnostic error evidence.
- No GM5 ordinary EC backend, FAN/APU/RGB/battery setters, startup writes or Linux
  EC transport admitted. No writes to770/7C3 or firmware/NVM in the new reader.
- Exact DSDT7D0/7D1 are DBD1/DBD2, written by1173 as Arg1/Arg2*8 to NPCF context;
  they are not admitted as percentage charge restart fields.

## Evidence and next dependency

User archive logs_GM5HG0A.zip has OS/ACPI/GPU inventory but no EC reads.
DSDT SHA256:64e288a1ed9e473368332202ace8fea0fe59506fbf527f5c43b611f32a45eee2.
Common local OEM MyFanCtrl.cs selects RamFan1p5 by78E.6 and ProjectID/commercial;
its presence alone does not establish this board's runtime values.
The owner needs to run this Windows binary on GM5HG0A and send a new dump.
Project740 / capability78E / RGB766 and raw tables determine the next adapter stage.
This stage is not a claim of complete hardware support.

## Verification scope

Common build via Tools/Build-All.ps1; Windows/Linux portable version and SHA256
verification are performed by the build script. Whitespace diff inspected.
No application launched, hardware tests or additional test suites run.
No GitHub package/publication requested for this change.
Final portable hashes are recorded below after the common build finishes.

## Final portable verification

- windows:0.1.142 SHA256 b8e9b5421d618a012a85eedea8dcd6ac2c2dbfe876255784c9e7230c0a27e45d
- linux:0.1.142 SHA256 086ca40a5d5eb934d14d40d2c542992bb255932cb68654ec9d5150c9ebef026a
