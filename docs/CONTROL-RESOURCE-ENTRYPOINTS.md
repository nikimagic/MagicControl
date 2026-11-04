# Shared command boundaries

GUI and CLI use the same device-write entries. A resource key describes an
operation, not evidence of a compatible device or permission to write.

| Domain | Shared entry | Backend implementation |
|---|---|---|
| Native/EC battery upper, restart, platform policy, voltage | executeBatteryResource | Existing NativeBatteryController or admitted Wujie battery API |
| Native charge upper/restart/expert pair, both OS | executeBatteryResource(LaptopControl,BatteryWriteRequest) | Exact known native policy, one read/decision/write/save transaction, checked LaptopControl setter |
| FAN modes/curves/restore/compound | executeFanResource(FanResourceContext,FanWriteRequest) | Existing GM/native/Wujie owners; command/readback together, asynchronous EC submission remains explicitly pending |
| Stock CPU profiles, Custom/editor/slots, automatic, economy, supply-limited legacy command | executePowerResource | Existing GM controller, native LaptopControl, Honor SMU or Wujie power API |
| Lighting modes/palette/RGB/brightness, raw restore and cycle ticks | executeLightingResource(LightingControlContext,LightingWriteRequest) | Private policy; existing GM keyboard API or admitted Wujie backlight API |
| HID lighting zones | Same lighting entry / Zone request | Existing ITE controller with fresh zone admission and per-zone configuration |
| Native brightness/timeout, both OS | executeLightingResource(LaptopControl,LightingWriteRequest) | Existing native setters and their own model bounds/readback |
| Host keyboard colour cycle | Same lighting entry / Cycle or Effect request | Existing KeyboardColorCycle; ticks re-enter as guarded Update requests |

Frontend policy and a device write are different operations. GUI battery
buttons map presets and remember the chosen view after a successful command.
CustomEditor/Preset requests enter the same CPU owner, which saves drafts and
sequences the existing FAN/power compound action. CLI converts external slots
1/2/3 to0/1/2 once, and uses the same checked writer/completion. Restore carries
its own origin and does not create a new user choice. These policies reach one
write entry; none contains a
second firmware command implementation.

Battery01.11: public applyBattery* policy paths, separate Windows/Linux Honor
files and raw-pair wrapper are deleted. UI Preset and PolicyButton are decoded
inside the existing Windows resource owner. Wujie fresh snapshot completion is
owned there for GUI/CLI/restore; native controllers retain their own persistence.
There is no remaining SettingsRememberChoice::Battery route. Lower APIs/save/
restore and model-specific0/100/60/97 meanings are preserved. A voltage command
still releases percentage control first; failure of its second stage does not
claim rollback of the completed release. Missing transports remain unavailable.

FANs01.11: three raw public writers, two per-backend workers and EC/draft wrappers
are deleted. GUI queue, CLI, external Wujie restore and existing Custom FAN step
use one request. Manual means autonomous EC; Online means host profiles in one
command. Caller operation lease is forwarded to existing enqueue methods.
Explicit Wujie Default resets APP consumers; internal restore/compound Default
keeps its narrow contract. Native ownership loss and failed readback cannot be
reported as CLI success. Linux retains its unavailable adapter, not Windows maps.

Lighting01.11: GUI, CLI and external restore share request semantics; CLI no longer
builds raw SingleZone RGB/effect changes. A raw Update retains the exact expected
snapshot, epoch and idle-blank flags used by lifecycle/cycle. Native pair failures
remain independent and are accumulated instead of overwritten by later success.
Explicit SingleZone completion uses rememberLightingChoice on a fresh observation;
GM CLI now persists the same observed values as GUI. Native/HID retain their own
persistence owners. Linux RGB/HID writers are not fabricated from Windows support.

No new register map, native mode value, protocol, driver selection, fan curve,
thermal step or setting schema was introduced in this cutover. Adapters retain
their exact-board, transport, ownership, range and readback checks. Unknown
typed backends cannot inherit Honor or Uniwill behaviour by matching a key.

`architecture_audit.cmake` is the unified source audit: it counts the concrete write definitions
and rejects direct writes in every GUI/CLI source, as well as bypasses in the
GUI battery and Custom policy functions. It is a structure check, not physical
hardware verification. Fake-adapter tests cover refused/malformed routes;
existing policy/queue/FAN/RGB tests check their admitted ordering and results.
Pre-cutover sources live in `archive/control-cutover-0.1.384` and are not built.

ResourceControlTable is the sole route-table implementation. Application-owned
OSD/Copilot preferences use their existing transaction and KeysPreferences
writer directly; they no longer create a one-shot binding to themselves.
GPU writes retain provider-ID validation and their single typed dispatch.

SETs/FANs30.10: frontend queues/workers/result ownership removed from the
four native pages. Platform models own typed intent/snapshots and call the SAME
entries above. Core ResourceControl Runtime owns finite workers; ResourceActor
registry owns dedicated permanent resource loops. OS window/GTK readiness and
pure input/render bindings remain UI-only. The old per-OS coordinators are not
kept as compatibility wrappers. No new device/protocol admission is implied.
