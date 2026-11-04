# GM6 configuration persistence,22.10.2026

## New owner log10
Archive: logs_GM6IR0C (10).zip;0.1.160;22:51:09–22:55:52 MSK.
At22:51:10 startup lighting reports explicit_file=false. Missing model INI
still prevents restoring EC policy; old percentage setter never saves GM6 upper.
At22:52:43 upper85/restart50 are confirmed with7C3=7,770=04. Battery stays88%,
0mA throughout this session. This does not prove the99→100 tail or95/90 hysteresis.
Slot1 first saves30/30/30 at22:53:09. Three later slot1 applications complete
with native_readback=true; intermediate direct Custom and Turbo also confirm.
Turbo0/0/0 denotes cleared overrides, not zero CPU power. No failed command
was found in this archive. Logs9 previously showed an actual reboot and platform
4→7 while the model preferences file was absent.

## Changes
- Exact GM6 shared per-SMBIOS UUID settings storage; one-time import under the
  exclusive launch lease, previous active portable before fresh archive defaults.
- Successful native power/physical cycle, FAN and Custom choices saved as typed
  desired commands, independent Custom draft and FAN overlay. Atomic checked
  writes; strict bounded parser and existing power/curve validators.
- Lifecycle uses existing model setters and shared DeviceOperation, admission,
  OEM checks, epoch cancellation, RMW/readback/rollback. No dump replay.
- Battery saves the pair and explicit4/FF or7/owned04 policy; bounded startup/
  resume restores upper/lower and policy. L16/GM5/Honor semantics unchanged.
- Static lighting plus optional explicitly selected Cycle6 resume; Windows
  device switches use shared WindowsDevices/STA radio access preparation.
- Diagnostic settings collection reads the durable files, including helper.
- Independent optional Intel PP0 cores vs Package display; no duplicate Package
  under CPU and no claim of whole-laptop measurement.

PP0 provenance (not copied code):
https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/master/LibreHardwareMonitorLib/Hardware/Cpu/IntelCpu.cs
MSR_PP0_ENERGY_STATUS0x639 vs package0x611, energy delta / interval. Reviewed
Raptor Lake only; error backs off30s, non-advancing zero counter stays unknown.

## Limits and verification
No requested automatic tests; no GUI/hardware/driver execution performed.
Build and staged-binary verification recorded separately after both OS builds.
EC readback is not physical charge cutoff/fan/power proof. Old choices that were
never written cannot be imported; select these once in the new version.
Restoration after reboot runs when MagicControl starts; no autostart service or
scheduled task is installed. Custom/Economy retain the AC-only command guard.
Other models do not use the new shared path/whole-configuration restore.

## Auto UI clarification
Auto resets only PL1/PL2/PL4 overrides, preserving the native operating/FAN
profile. The old GM6 selector never selected Auto after this command because
it always rendered the native profile. Now explicit Auto intent plus fresh
zero readback selects Auto. It persists across startup/wake. Restoring Auto
with a remembered Custom applies zero overrides directly, never briefly
replaying its old nonzero PL; the independent Custom draft remains unchanged.
The old raw-Custom capture also skips Auto and lifecycle restore so zeroes or
foreign boot RAM do not replace the remembered Custom.

No release archive or GitHub publication requested for this change; only the
required common build, working portables, verification and local commit.
