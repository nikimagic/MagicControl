# GM6 native button and CPU-only presets — delivery review

21 October 2026. Static source/package review and the ordinary common Release build.
No application, OEM executable, DLL, driver or hardware probe was executed.
No tests were added or run in this task.

## Changes reviewed

- Existing OEM WMI monitor admits exact GM6 EventULong. Successful subscription
  enqueues APP741.0 presence initialization in the existing controller worker.
- Event176 queues one explicit Office → Gaming → Turbo → Custom transition.
  Office/Gaming/Turbo use exact OEM factory FAN tables, native mode/indicator
  masks and zero CPU overrides. Custom uses its own curve intent/CSV and saved
  CPU draft or fresh firmware Gaming defaults.
- Shared DeviceOperation admission, exact identity, GCUBridge/GCUService ownership
  checks, readback and masked rollback constrain the writes. Suspend/close epochs
  cancel old requests, including stopOnline restore and rollback writes.
- Existing OSD presents command data; notification callbacks do no EC I/O.
- Shared CPU labels are English Economy / Office / Gaming / Turbo; only GM6 has
  a fifth Auto button. Custom replaces Manual only on the power card.
  FANs Manual / Manual online labels are unchanged.
- Explicit user choice: GM6 Economy PL1/PL2/PL4 =10/15/20 W; Auto=0/0/0 CPU
  overrides. Both use the existing CPU-only writer. Economy requires AC and a
  known PL4 scale. Auto retains the existing DC reset behavior. Neither command
  changes FANs, the native system profile or the manual power draft.
- Shared Geometry divides the existing row into four/five segments without a new
  renderer. Windows and Linux adapters use the same labels/count/geometry.
  Linux GM6 controls remain unavailable without an implemented transport.
- Native result publication invalidates stale SETs/FANs observations, preserving
  the distinction between an old command result and the current observed state.

## Preservation and checks

`prepare.py` captures before-staging hashes of personal INI/CSV/log/runtime data,
previous archived battery evidence, source runtime payloads and unaffected sources.
It checks the GM6 battery reader/decoder body against commit31620e8.
`verify.py` checks those hashes, all common-build application inputs, completed
build state, matching versions and SHA256 of GUI/CLI portable/build artifacts,
plus the embedded RCDATA payload bytes against their original runtime sources.
Neither helper executes the product, a hardware backend or a test executable.
Detailed results are in verification-0.1.130.json after build completion.

## Practical limits

New physical button delivery, green/blue/red/white LEDs, actual PWM response and
CPU enforcement have not been measured on GM6. Register readback is not such a
measurement. GPU offsets/TGP, NVRAM, TCC, voltage, memory and battery side effects
from the complex OEM whole-profile method were not copied. This implementation
covers the supported FAN/CPU-policy/indicator portion and reports errors rather
than guessing mixed flags, invalid Custom input or unconfirmed write rollback.

The separate percentage-search report documents why other Uniwill percentage
interfaces and HydroControl captures do not establish a working GM6 charge gate.
Production battery policy and the GM6 battery reader body are unchanged.
