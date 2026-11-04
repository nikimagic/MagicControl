# MagicControl 0.1.175

Core LPP API and Windows BLE/GATT/native-policy bridge compiled into both-platform
build graph; no UI. Exact HYDROC, two native device names, NUS service/TX/RX,
Notify+sw handshake, separate pump/fan commands and flow observations. Manual,
native Auto, stop, sleep, line-off/disconnect and existing-session lifecycle.
Linux unavailable adapter; no scan/connect/output replay at startup or in dumps.
Native presence7C7.1 owned masked RMW and fixed bounded WMI notification; preserve
P13, neighbour bits, internal FAN tables, CPU overrides and battery semantics.
Auto preflight requires existing FAN Default/CPU Auto and fresh normal flow.

Shared helper has explicit native-notification scope; previous query indices
retained. Controller records accepted intent versus connection/flow/unknown
physical state. BLE loss/timeout/epoch cannot silently replay output commands.
Current docs: LIQUID-COOLING.md. OEM source hashes and decoded sw proof are in
sources.json / ble-strings.json; original OEM was never executed.

Build-All complete, both portable folders updated. Four GUI/CLI hashes and
versions match build output; application inputs match current source and all377
personal settings files are preserved. Compiler warnings, no errors. No tests,
app/CLI or hardware probes; physical BLE/EC response remains unmeasured.
A follow-up static-review fix will move critical stale-flow cleanup ahead of
background DeviceOperation scheduling; public LPP API has no UI consumer yet.
