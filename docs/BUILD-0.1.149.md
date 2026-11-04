# MagicControl0.1.149 — Windows local instance handoff and PawnIO version

Owner requests a newly launched executable to replace an older running copy
through normal shutdown. Compare incoming and running local VERSIONINFO only;
GitHub catalogue/release availability is irrelevant. Reuse updater numeric/SemVer
comparison through compareVersions; no network check is made by startup.

- Windows InstanceLease serializes launches and keeps the global instance mutex
  across MainWindow destruction. Inspect the actual window owner's pinned process
  handle/path and ProductName/type/version resource. Same/older launch activates
  the equal/newer process; newer launch requests normal cleanup and waits for that
  process to end before acquiring the instance mutex and creating any controller.
-0.1.149+ exposes a registered graceful-exit query/request. Legacy0.1.62–148,
  including0.1.90, already implement WM_QUERYENDSESSION and targeted WM_ENDSESSION
  through DestroyWindow/onDestroy. Use only that known range after consent.
  Historical source f3bf37a0.1.62 confirms the handler. No broadcast/TerminateProcess,
  no WM_CLOSE hide-to-tray or overlapping EC controllers. Bounded60s exit wait;
  unknown/refused/timed-out close leaves new hardware startup blocked with a dialog.
- Reuse normal tray/session exit cleanup. Respect existing FAN persistence,
  cancellation/worker joins and settings. No new EC writes or reset on takeover.
- With AllLogs enabled, one overwritten launch-handoff.json records incoming/
  previous versions, executable paths, PID, request/result/error. Successful new
  session also logs its admission. Never rotate another process's main log before
  ownership, and never replace its settings with the incoming portable defaults.
- PawnIoDriverStatus now resolves the SCM image path and reads installed SYS
  VERSIONINFO through a common static file-version helper shared with handoff.
  Startup log and dependencies.json before/after dump report file_version,
  resolved_image_path and version_status, plus bundled setup2.2.0 separately.
  File read failure remains explicit and does not erase SCM installed/running.
  No driver/service/device IO. This is file version, not in-memory kernel version.
- New failure dialogs use existing bundled-font message dialog and four-language
  translation catalogue. Linux receives common comparator/diagnostic schema and
  matching product build; its GTK application activation behavior is unchanged.

Common Release Windows/Linux; stage both working portable and compare hashes.
No tests added/run, GUI or hardware commands executed. Actual legacy takeover
still requires launch on the owner's machine; only compilation/static inspection
and package/resource verification are performed here.

Verified staged portable:
windows 0.1.149 SHA256 F2E441EFA1993A677AD96BBC3744BC39498236A9CCA3BE63AB4F6D25002C8D20
linux 0.1.149 SHA256 C5880E272C8531BD9D9F2B22DA5C3EBE07EFD205BF176E20D71235C99FBDA078
