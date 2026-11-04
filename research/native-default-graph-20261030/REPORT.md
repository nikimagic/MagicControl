# Wujie native Default graphs — 0.1.396

30 October 2026. Implements the owner's read-only request using the firmware
analysis in ../wujie-default-curves-20261030/DECODED.md.

## Data and presentation

- Exact Wujie X5SP4NAG profile and live identity 82/72/26 only. Live native
  header 62C0 and secondary table 62C4 must match; context is checked before
  and after reading. Manual, Boost, override, asleep and unknown selectors
  do not produce a native Default graph.
- Core NativeDefaultFan supplies firmware-decoded base curves and observed
  context to the accepted ResourceState. Both graphs use the selected EC CPU
  control input (43E in the observed Turbo state; 9178 on the alternate path).
  Neither ADLX GPU temperature nor PawnIO Tctl/Tdie substitutes for that input.
  The zero GPU-input table does not define an independent GPU-temperature curve.
- First request is observed at 670. Second request is calculated using the
  decoded secondary transfer and actual primary PWM; it is visibly marked
  approximate. Actual PWM is separately read at 75B/75C. Hollow request and
  filled actual markers use the same accepted control temperature.
- Graphs describe base requests, not every stateful hold, floor or override.
  Start 48 C and stop below 47 C are shown explicitly. A held actual/request
  value can be above the base line during cooldown.
- No new worker, activation, mailbox or EC write is added. The reader joins
  the existing telemetry collection. Passive reads do not enable per-byte audit
  logging. Stale curves expire after 2.5 seconds instead of retaining a false
  current temperature. Dense curve points are excluded from periodic logs.
- Manual drafts, fan-curves.csv, Custom slots, fan commands, power limits and
  editor bounds remain unchanged. Windows/Linux use the existing graph renderer
  and geometry, with one common formatting helper and four UI languages.

## Validation

The common Build-All run completed for Windows/Linux GUI and CLI at 0.1.396.
Both working portable folders were updated; all four SHA-256 values match
their build outputs. See verification.json for exact hashes and source fingerprint.

The actual Windows read-only `mctl fans status --read-only` result is saved in
cli-status.json. Both channels contain 81 base-curve points. Resource projection
compared 21 values with zero mismatches. At EC CPU 45 C, request/actual were
25/25 percent for the first fan and 0/0 percent for the second, consistent with
native hold behavior rather than a claim that the rising base line is the target.

The owner confirmed both curves and labels are visible and supplied the
0.1.396 screenshot. It shows Default Turbo, EC CPU 46 C on both graphs and
request/actual 0/0 percent for both. Fresh GUI logs independently contain the
accepted native resources (including EC address 0x43E) for both channels.

No heating sweep or control-command regression run was performed. Linux was
compiled and staged, not physically validated on Wujie; no new Linux EC transport
was enabled. Compiler warnings remain, including a signedness warning in the
timestamp freshness comparison. Unknown boards retain their existing behavior.
