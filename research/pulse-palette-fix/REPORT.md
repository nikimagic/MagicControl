# Native Pulse palette and HID Breathing RGB, build0.1.188

GM-family native Pulse uses four OEM BreathingIndex values1–4 from the active
GM6 decompilation, `MyControlCenter.MyRgbKeyboard.SingleZone.StartBreathingMode`.
The UI had disabled every Pulse colour button and rejected the handler. The
shared model palette now exposes four numbered presets without guessing their
physical colour names. X5SP4NAG keeps its measured White/Red/Green/Blue map.

On ITE HID, selecting a palette or custom RGB value now preserves Breathing
effect2. Previously the handler changed it to Mono1. The RGB selection indicator
recognizes both effects. Existing availability, ownership and busy guards stay
in effect. Card/header/scroll geometry and EC protocol are unchanged.

Tools/Build-All.ps1 completed0.1.188 for Windows and Linux GUI; both working
portable folders were updated. `verification.json` checks the build receipt,
all captured application inputs, both portable versions and executable hashes.
No application, hardware helper or tests were launched. Native preset readback
and physical colours remain to be checked by the owner on the respective model.
Linux received no unsupported native/HID hardware writer.
