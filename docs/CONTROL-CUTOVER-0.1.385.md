# Resource control cutover — 0.1.385

The remaining GUI/CLI battery, Custom and RGB writes use the shared entries
in `CONTROL-RESOURCE-ENTRYPOINTS.md`. Pre-cutover sources are retained in
`archive/control-cutover-0.1.384`; no product target includes that directory.
The preceding observation cutover is recorded in commit `dff789cb` and its
test-only reference archive. The runtime shadow/toggle is absent from `src`.

## Preserved contracts

- GUI battery view/preset persistence stays after the successful device write.
- CLI retains its explicit values, platform policy and native slot numbering.
- GUI Custom retains draft persistence and the original FAN/power sequence.
- RGB changes and receipts use the original guarded GM/Uniwill API; HID zones
  and typed native brightness retain their own confirmed device interfaces.
- Driver/protocol code, native battery controller and FAN implementations were
  not changed relative to the preceding 0.1.384 product source (`35c21573`).
- All 58 pre-existing files in the Windows/Linux portable `settings` folders
  matched their pre-build SHA-256 after staging. No user config was replaced.

## Verification

The final relevant CTest runs passed 18 Windows tests and 17 Linux tests with
zero failures/skips. They include command-definition/bypass audits, resource
state, the core settings suite, queue/failure handling, Honor battery policy,
GUI fixtures, and Windows FAN suspend/resume and worker-start failure paths.
Temporary test files were kept under the build directories.

The structure audit scans GUI/CLI C++ sources and headers, counts the common
device-write definitions, and checks policy functions for direct bypasses.
Registration never grants hardware permission; each real adapter retains its
identity, ownership, range, transport and readback checks.

Tests use injected collectors/EC or typed fixtures where hardware commands
would occur. The actual supported devices were not physically exercised by
this run. A previously observed intermittent fake suspend-test failure was
not reproduced by the final runs; its original cause is not established.
No thermal algorithm was weakened to make that test pass.

## Product build

`Tools/Build-All.ps1` completed 0.1.385 with both platforms marked `verified`
and overall status `complete` in `build/product-build.json`. GUI and CLI were
updated in both working portable directories and matched their build outputs.
No GitHub archives or publication were performed by this build.

Source fingerprint:
`6d6d1f855c13075740af8e0dfc2c7692b1d7c97ea9831970747eb6d31de1ba76`

| Working binary | SHA-256 |
|---|---|
| MagicControl/windows/MagicControl.exe | 6554A82DBB22774BF34554EF8C701B57644FA2C5189A7E1E2DB4A672E9069613 |
| MagicControl/windows/mctl.exe | 89A7EA31856A7E94634338C90FEB3CF7CB4C0C10DA562A478DAC7F1935E69A17 |
| MagicControl/linux/MagicControl | 69CBBA387B9936130F7A25406E991C527315EA33E9FE511133D71436E1E7ED45 |
| MagicControl/linux/mctl | 49976FAA692FAEA86426B779E9A93573E00F350B49C24B4E58CA471E57E6A856 |
