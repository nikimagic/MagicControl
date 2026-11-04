# Wujie Default curves: feasibility and actual data sources

**Correction after deeper tracing30October2026:** the initial98F8/6598
interpretation below was incorrect: that is APU power policy, not native FAN
curve selection. The actual native FAN path is878E -> XDATA8E7 -> CODE62C0,
with calculation8986 and second-fan mapping62C4. See **DECODED.md**,
native-curves-decoded.json and live-native-fans.json for the authoritative
decoded result. A passive live read confirmed the actual table pointer.

30 October2026. Read-only saved firmware/OEM source review. No request to the
EC, no table-export command, fan switch, product edit, version change or build.

## Result

The actual Default algorithm has a recoverable firmware data source. Its base
piecewise-linear requests are now decoded (DECODED.md), but are not yet exposed
by the application. Full loaded/transition behavior is not physically validated.
Do not label the inactive F00 RAM payload or OEM RAM template as the active
Default curve. Current GUI telemetry points remain real observations; an
entire native curve requires decoding the native selector/output algorithm.

Two distinct paths are now identified in the exact saved EC update payload:

1. Factory RAM-template export: bank2 8BF9 recognizes F5D=FD/F5E=C9 and a
   signed mode range1..7 atF5F. It prepares CODE6204 as the source pointer,
   calls98F8 withR7=0, then copies96 bytes into XDATAF00 through the generic
   memory-copy routine6EDD. This is an ACTIVE mailbox command, not a passive
   register read. It also calls the native parameter selector and is not
   established here as a side-effect-free query. No command was executed.
2. APU power parameter selection (corrected interpretation): bank2 98F8
   initializes a CODE pointer6598 and contains further pointer choices. At
   9B9E it publishes baseline fields to730..737/7A7..7AA. Other branches write
   8C0/8C1/8C3, which AA1B..AA3C copies to86B/86C/86E: the known STAPM/Slow/Fast
   power fields. This is not a fan-temperature/PWM curve.

CODE6204's96 bytes contain two48-byte Up/Down/duty-like arrays. CODE6598's
initial APU power-policy block has a different compact layout. Their byte
contents and the historical RAM mirrors are recorded in evidence.json.
The saved29October RAM730..737 starts36 36 41 01 2D 2D 41 01, matching the
initial power-policy table's corresponding base values. This is historical data,
not confirmation of which branch is selected in today's screenshot.

## OEM corroboration and limits

Exact recovered5.57 GCUService source has RefreshDefaultFanTable at44366:
write3935(F5F)=mode;3933(F5D)=253;3934(F5E)=201; wait500ms up to10times;
on acknowledgement call GetEcFanTable to read the RAM data.
That particular method is in BTCooling/LiquidHWOC and is not automatically
the active manager on this Wujie. The existing RamFanFactory.cpp implements
the reviewed GM/HYDROC mailbox path; it must not be transplanted/admitted just
because a ProjectID number or similar mailbox exists.

The exact Wujie firmware does contain the matching mailbox signature. That
confirms a real static mechanism, not live request success or equivalence to
its autonomous native Default algorithm.

## Before drawing Default as a whole curve

- Trace the actual selected ROM table and native CPU/GPU PWM calculation,
  including hysteresis, clamps and non-temperature conditions.
- Confirm which parameters are readable as current EC mirrors and what can
  be derived from them without issuing the active export command.
- Account for AC/DC and the actual performance policy; do not reuse a single
  template as every native mode's curve.
- Expose typed observed/default-curve data through core, then let the existing
  graph render it read-only. No new model-specific graph or manual-curve edit.
- Preserve unknown/unavailable when a complete native curve is not established.

Source SHA256 is in evidence.json. As elsewhere, the embedded EC update
payload is not an independently read live EC flash image.
