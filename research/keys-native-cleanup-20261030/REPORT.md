# One native Copilot operation and KEYs audit,0.1.393

CopilotKeyBlocker.cpp/.h were deleted. Product source contains no
CopilotKeyBlocker/copy of copilotBlocked, F23/Win+C suppression, periodic
synchronizeFirmware or MainWindow Copilot action. The OSD F24/lock-key
observer remains; removing it would break unrelated notifications.

One resource binding in KeysHardwareWindows targets applyCopilotFirmware.
GUI and CLI use executeKeyHardwareCommand; lifecycle uses the same
applyKeyHardware resource entry and its own fresh readback. One context
chooses one admitted native transport. Wujie/known Uniwill use their existing
EC/typed EC API; other firmware can expose the source-verified keyboard WMI
ABI. There is no software blocker or parallel watchdog.

The native register-level functions in QuickSwitch.cpp and the typed
gm6ir0c controller were not changed. Shared741.0 is held for native On and is
never cleared by Off. The adapter requests one idempotent lifecycle reconcile
when effective Off may hide a latched728.2 under APP0. Other KEYs setters do
not gain repeat calls. No FAN/RAM/PL command was added to this path.

KEYs now displays actual Copilot readback rather than an application preference.
CopilotBlocked stays in the existing Version3 keys-settings.ini. Legacy import
still reads raw old keys; the obsolete ApplicationPreferences bool is removed.
Linux keeps its unsupported hardware state and does not fabricate a blocker.

The audit of all11rows found/fixed CLI omissions too: confirmed CLI actions
now share the GUI persistence/readback handler; Wujie camera uses the same EC
context; Linux Fn/microphone use the same executeKeyOperation as their GUI.
Bluetooth lifecycle preservation, other native masks, radio access policy and
Win Lock one-shot/settling behavior are unchanged. Full matrix:
docs/KEYS-AUDIT-20261030.md. Current instructions and polling inventory updated.

## Verification

- Common0.1.393 GUI+CLI build completed on Windows and Linux. Linux initially
  revealed one old3-argument OSD preference read; that caller was fixed and
  the same reserved393 build resumed. Windows artifacts were unchanged by that
  Linux-only correction. No extra product version was allocated.
- All four real portable artifacts match their build hashes and version.
- Elevated new CLI read10hardware/system states on the actual Wujie with
  --read-only; all exited0. Copilot returned true/available via Uniwill EC,
  camera confirmed the EC-disabled state, microphone returned false via
  Windows Core Audio. No switch was toggled by the collector.
- Static product scan finds zero old blocker/hook/watchdog/App DTO symbols
  and one definition each of applyCopilotFirmware, applyKeyHardware and its
  Copilot resource binding. No MainWindow Copilot action remains.
- Existing projection/policy expectations were corrected. Test suites were
  not run or extended without a request, per AGENTS.md. No physical press or
  sleep replay is claimed from source inspection or the getters.

Hashes, observed states, settings differences and exact verification scope
are preserved in verification.json. User settings and unrelated WIP remain
outside this change. No GitHub package or publication was requested.

## Subsequent physical confirmation,30 October2026

The owner answered the explicit393 manual protocol: both On (short press and
5-second hold blocked) and Off (key works) behave correctly; final state On.
The GUI session2026-10-30T05:18:14.078Z/87909389133400 is0.1.393. Native Off
at08:19:05 Moscow/action6 and On at08:19:17/action10 both report confirmed
readback. Selected immutable events and the owner confirmation are saved in
physical-validation-0.1.393.json. This validates the physical key on this Wujie
after removing the software hook; sleep/hibernation and other devices were
not tested by this confirmation. No product code/build change was required.
