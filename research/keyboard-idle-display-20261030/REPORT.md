# Keyboard idle readout,0.1.392

The lighting card now has a read-only field at the right of its heading:
`Выключение подсветки через [value] сек`. Existing card bounds, buttons, scroll
and fan/power editor ranges are unchanged. Shared translated geometry measures
the embedded font and scales with DPI; both GUI surfaces reuse existing numeric
controls with editing/arrows disabled. Russian/English/SC/TC labels are present.

The new core `readKeyboardIdle` is admitted only on the exact verified Wujie.
It reads a stable high/low/high native counter, platform-dependent30/600 limit,
APP/idle flags, power state, host-disable and AC/DC/current brightness slots.
Its implementation has no write/trigger/persistence call. The existing RGB
getter/setter, saved lighting settings and controller commands are unchanged.

Windows uses the existing SETs worker and operation admission. Timer ticks1sec;
the two intermediate ticks collect only idle observation. Full SETs/SMU polls
remain on their existing nominal3sec schedule. Existing5sec tray refresh and
command/power event refreshes can still request extra full polls; this task did
not redesign the global read scheduler. Fast observations use a small audit
state and do not relayout power selections or restore an RGB cycle.

Field states: remaining integer seconds when active; `Отключено` if APP741.0 or
host-disable78C.1 inhibits the native timer;0 if idle flag is expired (not a
physical-darkness guarantee after adapter relight);em dash if unavailable,
asleep or older than2.5sec. Power switches do not rewrite the countdown or
power limits. On AC and DC the actual working level is826, not stale78C alone.

Linux and other models display the same unavailable field until their exact
reader is admitted. No Wujie register map was transplanted into Honor, HPBSD,
HID or another Uniwill board merely because of a similar interface. CLI
`keyboard status` exports the same core observation as an `idle` object.

## Verification

- One common0.1.392 build completed for Windows/Linux GUI+CLI; four working
  portable hashes match the build outputs. See `verification.json`.
- Elevated new CLI `keyboard status --read-only` succeeded on Wujie:
  Platform7,limit600,elapsed0,APP741=81,AC=true,AC/DC/current levels4/1/4.
  Thus the current native timer is inhibited; the UI should say Disabled.
- Elevated `power status --read-only` succeeded: fresh EC and SMU both reported
  Fast35/Slow30/STAPM30 W in that separate current snapshot. This is not a new
  claim about factory AC/DC watt tables or a command to change Custom.
- No new tests were added/run, per AGENTS.md. GUI startup and EC writes were
  not used for verification. Actual100/200% window screenshots were not taken;
  layout follows the existing shared Scale and embedded-font measurement.
- Linux settings baseline stayed unchanged. Two Windows settings files changed
  while the user was operating the app (device preferences and Custom library).
  Their live contents were retained and excluded from the commit.

The requested inventory of background reads is in `docs/PERIODIC-READS.md`.
Earlier native expiry/physical confirmation and adapter relight evidence is in
`research/wujie-wake-backlight-20261030/REPORT.md`.
