# Post-cutover cleanup — 0.1.386

## Removed or consolidated

- Deleted the unreferenced `NvidiaGpuCard.h` include alias.
- Deleted the uncalled `checkBiosUpdate` implementation and its Windows source
  target entry. Removed its unused WinHTTP link dependency. This was a dormant
  OEM OTA query, not the live application updater or BIOS dump implementation.
- Renamed the remaining support-link data header to `BiosSupport.h`, removed
  the unused BIOS-query declaration and two unused metadata fields. Visible
  support captions, URLs, notes and exact board matching are preserved.
- Removed the two unused ACPI probe aliases from the private platform header
  and implementation. The actual transport class, factories and I/O ABI remain.
- Removed duplicate `ResourceControlSession`/self-binding machinery.
  `ResourceControlTable` is the sole generic route-table implementation. GPU
  callbacks retain exact provider-ID/value validation, while application-owned
  preferences retain their original apply/save/finalize and rollback order.
- Centralized SETs device-write descriptors with the existing common operation
  descriptors; registrations no longer copy their string IDs.
- Replaced eight separate architecture-audit scripts and the new cleanup guard
  with one cached-source audit. It retains all prior definition/bypass/state
  checks, verifies 15 concrete definitions and rejects retired APIs/files.
- Archived the old execution-history plan and replaced its live document with
  a concise description of the current architecture.
- Removed the clean temporary `resource-cutover` checkout after checking that
  all three commits were patch-equivalent to changes already in `main`.
  Its branch/recovery history remains; unrelated worktrees were left alone.

Pre-cleanup source and audit scripts are retained in
`archive/cleanup-0.1.385`, excluded from build targets. OEM SDKs, driver/runtime
bundles, BIOS/EC dumps, research and user settings were not cleanup targets.

## Verification

Both checking configurations built GUI, CLI and the selected integration tests.
The final runs passed 11 Windows tests and 10 Linux tests, with no skips or
failures. The lower process count compared with 0.1.385 is due to consolidating
the eight static audits into one; their checks were retained, not disabled.
The dynamic suites still cover core settings, resource state, typed policies,
GUI fixtures, queue failures, FANs and suspend/resume.

The first Windows run reproduced a test failure with restored ownership,
`gate=0` and `paused=1`. The test re-read the transient pause counter after its
successful completion predicate. The normal online FAN loop legitimately
acquires a scoped ObservationPause without acquiring DeviceOperation. The test
now checks gate/pause from the accepted completion sample and independently
checks that ownership remains unchanged. Its completion predicate, timeout,
FAN implementation and safeguards were not weakened. This explains the
reproduced counter race; it does not establish the cause of every older flake.

No device/protocol implementation or EcAccess pause primitive changed. The
ACPI backend edit only removes the two unused free helper functions. Physical
commands on the supported machines were not exercised by this test run.

All 58 pre-existing portable settings files retained their pre-test SHA-256
after the tests and product staging.

## Product

`Tools/Build-All.ps1` completed GUI+CLI on both platforms as 0.1.386. The product
record is `complete`, both platforms are `verified`; every working portable
binary matches its build output. Both CLI binaries independently report
0.1.386. No GitHub packaging/publication was performed.

Source fingerprint:
`09595a4ac8e175c30039cec31ddf570b1a685131b9e20e3008bf5818199383b4`

| Working binary | SHA-256 |
|---|---|
| MagicControl/windows/MagicControl.exe | 007F6C1109980FAD46E70A12A75B421EAA5E25A619253D54F90E81B778D6A61D |
| MagicControl/windows/mctl.exe | 3D18CFCFAF53C307CAC972234015F7BCB05A4834DF91A3E17339B964514E2284 |
| MagicControl/linux/MagicControl | 8D5038E1808CD97746FE70FACBFF57229A8A3C94A7535E947B91647B339F64DE |
| MagicControl/linux/mctl | FAA99289F98A8DD8432E0B8020072AC19E3CBDDB5A79B47688439B5A8DDF596A |
