# Battery aging override: agreed behavior and integration evidence

Status: **implemented in MagicControl0.1.415–0.1.416**. Production owner is
src/core/src/BatteryAging.cpp, reached through the shared charge.aging route.
The requested EC-card switch and thermal exposure row are implemented. Windows
has the exact admitted Wujie I2EC backend; Linux controls remain unavailable
until its transport is validated. The notes below record the original design.

Owner decisions, 31.10.2026:

* Put «Подменить показания батареи · Выкл. / Вкл.» on the battery card in the
  row near the cycle count and health, with the existing common visual style.
* On: use the EC alternate date for one month of calendar age and reset its
  accumulated high-voltage history. Maintain one month at application startup.
* Off: restore the captured original alternative-date bytes and full real
  exposure history, including new accumulation (owner clarification).
* Operate through the existing resource/operation gate and model backend;
  settings and fan control on other devices must remain unchanged.
* Show the EC exposure counter as «Термочасы» in the primary EC battery card
  shown by the owner. This is the raw temperature-weighted exposure count,
  not elapsed wall-clock hours. Do not invent a value for unsupported models.

Only Wujie X5SP4NAG EC identity 82/72/26 has a verified device test. Other
models must not acquire this register map merely by sharing an OEM or UI.
Both Windows and Linux UI must retain their common rendering/language/DPI
contracts; an OS without a validated transport cannot advertise a write.

The existing Windows Wujie transport uses ECRR/ECRW and has no I2EC RIOP
facility. The proven research write uses the exact UWACPIDriver hash and
two-argument RIOP, with an I2EC data-port write. The installed driver's
three-argument WIOP helper corrupts an argument; see
../../docs/UWACPIDRIVER-ARGUMENTS.md. A production adapter must reuse the
existing EC transaction mutex and device operation gate and keep exact
identity/hash/address/mask checks. Do not run the PowerShell experiment from
the GUI or add a parallel unsynchronized EC path.

The `0x9C9/0x9CA` pair is big-endian. Read it twice through the admitted I2EC
path and publish one optional measurement in the existing EC battery snapshot;
do not claim that a raw 0800–0BFF H2RAM read is a reliable source. The current
card layout contains six matching detail rows per source, so adding a row must
update the shared row count, height and both Windows/Linux layouts together.
Absence, unsupported transports and failed reads must remain unknown, and
temporary missing data must obey the existing EC display-cache policy.

The first toggle must preserve original7F2/7F3 and9C9/9CA before *any* write
in an atomically written, durable per-battery record under settings/; include
enough verified identity/context to prevent applying a backup to a different
battery. Retain a recovery marker across interruptions and verify readback.
Do not replace the original record during repeated on/startup/resume. A failed
transition must preserve the record and expose an actionable failure, not
present a successful toggle. Native726.bit0 requests a firmware save; its
other bits, in particular the fan bit, must be preserved. An idle queue is not
independent proof of nonvolatile device50 persistence.

This Wujie already has alternative date5D21 and history0 due to separately
authorized experiments. Its real pre-change snapshots exist:

* `september-age-20261031-084228-c43d2d71/before.json` contains both the
  original7F2/7F3=00/00 **and** history9082. SHA-256
  `b7016b8766773f9d3610934541da4d80ac343596042fb8622567ef33ba7f14c3`.
* `history-reset-20261031-091644-ff1d9b02/before.json` corroborates history9082
  after the alternate date was set to5D21. SHA-256
  `184af94637d4a9d445828f872167738bea582d76f568f65780988c1e294deff2`.

Fresh offline verification confirmed both hashes, board WUJIE Series-X5SP4NAG,
driver hash6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235,
manufacture bytes5A52 and cycles44. The first snapshot alone contains the
complete original pair. Blindly capturing the current state as the
original would make Off restore the override instead of the original. Any
one-time import needs to validate the original snapshot, its recorded hash, board,
manufacture date and matching current applied state; no generic import to
another device is safe. Restoration of these values through the prepared
research script has not been physically tested, and no independent device50
backup or power-loss persistence test has been performed.

The current interface date is the original manufacture date, whereas the
requested switch changes the EC's alternative date for its aging calculation.
The high-voltage counter naturally starts accumulating again after reset; the
owner requested maintaining one-month age, not repeatedly erasing history on
every telemetry poll. Explicit startup/recovery policy for history must preserve
the original snapshot and avoid repeatedly resetting an active device.
