# Default fan card flicker — 0.1.397

## Cause established from 0.1.396

The owner's screenshot shows valid native Default Turbo curves, while the owner
reports that the cards intermittently clear. Actual presentation logs contain
352 frames, including 20 missing native snapshots. The median frame interval
is 2.025 seconds, the longest is 5.588 seconds, and 104 intervals exceed the
old 2.5-second graph expiry. See before.json for exact missing-frame examples.

Two display-lifetime errors combine: a skipped native read was indistinguishable
from a confirmed non-Default context, and a normal collector delay exceeded the
curve lifetime. Native EC data was also read before potentially slow generic
monitoring providers, ageing the native sample before it reached the UI.

## Change

- The core read result distinguishes Available, Unavailable (paused, failed or
  inconsistent read) and Inactive (confirmed incompatible mode/identity/table).
- Existing MonitoringDisplayCache bridges only Unavailable frames for at most
  ten seconds, preserving the original timestamp. Inactive clears immediately.
  The retained snapshot is display-only and never reaches a control loop/setter.
- A retained snapshot or one older than 2.5 seconds is labelled “last reading”
  in all four UI languages. Ten-second expiry still applies during a real outage;
  repeatedly retaining a sample never renews its timestamp.
- Windows collects generic telemetry before native EC data. Both Default graphs
  retain their exact EC control-temperature source and separate request/actual.
- Coherence checks compare the relevant FAN/selector bits of shared registers,
  not unrelated keyboard/USB fields. Admission and mode checks remain active.
- Existing fan commands, Manual curves, Custom presets, power limits and stored
  settings are unchanged. Both GUI implementations use the common core cache and
  formatting helper; there is no extra GUI timer or hardware write.

## Verification

Common Build-All completed 0.1.397 for both OSes and both GUI/CLI. All four
portable binary hashes match the corresponding build outputs; exact hashes and
source fingerprint are in verification.json. Read-only Windows CLI confirmed
both native Office curves, EC CPU 46 C and 81 points each; projection verification
compared 21 values with zero mismatches.

At build completion no new GUI 0.1.397 session was present. The owner subsequently
confirmed in chat: the cards no longer clear or flicker in Default.

Follow-up logs contain 362 accepted GUI presentation frames, from 15:35:58 to
15:58:37 Moscow time. Of these, 25 explicitly carry retained native readings,
showing the continuity path in use. Another 43 frames have no native graph;
this aggregate spans different contexts and does not identify them as read errors
or visible flicker. The compact summary alone cannot classify each missing frame.
The final fresh Office frame reports EC CPU 58 C on both graphs, primary
request/actual 33.5/33.5 percent and secondary 28.5/28.5 percent. Both curves have
81 points, and neither final reading is retained. See updated verification.json.
The 0.1.396 missing-frame total includes all observed contexts, not a claim that
every missing frame was a bug (confirmed mode transitions may legitimately clear).
Linux validation is compilation and portable staging, without a new EC transport
or physical Linux hardware run. No heating or fan-command sweep is part of this fix.
