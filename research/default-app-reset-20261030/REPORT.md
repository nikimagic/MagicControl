# Explicit Wujie Default: release all APP consumers through core

30 October2026. User instruction: Default resets every APP consumer, including
Copilot, then releases APP; preserve power limits and saved Custom presets.

## Implementation

- Core ApplicationPresence.cpp/.h owns the operation and the fresh consumer
  snapshot. Exact Wujie82/72/26 admission; no foreign model register mapping.
- Existing narrow restoreNativeFanMode clears FAN commit/custom/override/boost
  masks only. Tables, channel layout, CPU selector and PL fields are preserved.
- The existing copilot-lock resource is injected by the controller and invoked
  through applyKeyHardware. No second forward native setter, hook or timer.
- Core checks consumer masks, clears741.0 LAST and confirms readback. It compares
  CPU751.maskB0 and requested PL783/784/785 with the pre-operation values.
- CopilotBlocked=false is saved through rememberKeyPreference in the dedicated
  KEYs schema. KeysLifecycle loads that file after its DeviceOperation admission.
- A masked journal supports failure rollback; callback power/epoch exceptions
  retain their original type and stop further writes after sleep/interruption.
- Explicit Default runs even if FAN flags already show Default. GUI and CLI
  share the same controller entry and core operation; GUI source is unchanged.
- Internal recovery/diagnostic Default stays narrow and preserves KEYs. The
  configuration-restore cleanup explicitly requests that narrower operation.
- Startup/resume presence reconciliation no longer unconditionally sets APP.
  Existing Manual restoration remains with its owner; only a latched native
  Copilot request can require presence from this reconciliation path. Retained
  RAM tables cannot be activated by the passive presence check.

## Preserved scope

No changes to power setters, CPU presets, PL values, curve values/encoding,
Custom slot files or other KEYs choices. No curve/table writes in the new core
operation. GM/Honor/HPBSD hardware paths are untouched. The core compiles on
both OS; the changed live controller binding is the existing Windows Wujie
controller. Linux's unavailable direct EcAccess transport is not enabled.

AGENTS.md, FAN_MODES.md and register/API comments document explicit Default
versus narrow internal cleanup. No process-exit hardware reset was introduced.

## Validation

Common product build0.1.395, Windows/Linux GUI+CLI. Exact portable versions and
four binary hashes are recorded in verification.json after Build-All finishes.
No new tests or test runs were requested, so none were added/launched. No GUI
or hardware-write command was launched by the agent. The owner was asked to
open the updated Windows portable and press Default for physical verification;
that result must be distinguished from successful compilation/source checks.

User INI/CSV files, logs, pre-existing deletions and unrelated work were kept
out of this change. GitHub packages/publication were not requested.

## Confirmed on the owner's Wujie

The owner launched0.1.395 and pressed explicit Default. Log action5 at
09:36:14.745Z (12:36:14.745 Moscow) entered the core reset with APP=1 and
all FAN/Copilot consumer masks already clear. The previously skipped no-op
case therefore exercised the new path. At09:36:14.916Z it confirmed APP=0,
consumers=0, Copilot Off saved, power_preserved=true, power_writes=0 and
table_writes=0. Core execution took170332 microseconds.

Following settings snapshots show741=0x80 and the real EC elapsed count
progressing16->22->27->32 seconds (limit600, DC, brightness4). The owner
explicitly confirmed that the countdown appeared. keys-settings.ini contains
CopilotBlocked=0. Raw selected-state observations and the confirmation are
preserved in owner-default-observation.json.

Both Windows/Linux portable GUI+CLI finished0.1.395 and all four binaries were
independently SHA256-matched to their compiled outputs. Windows CLI --version
reports0.1.395 without hardware access. GUI source has no changes. Physical
sleep/resume and other models were not tested; their absence must not be
confused with the successful live Default/EC-counter confirmation above.
