# MagicControl 0.1.176

Final source review moved critical BLE-loss/abnormal/stale-flow cleanup before
ordinary background DeviceOperation admission. A busy UI operation no longer
prevents this decision. Actual EC cleanup is still serialized by the shared
typed controller and changes only the owned7C7.1 field. A common abort path
preserves cleanup errors/uncertainty and never retries output commands.

LPP core/API/Windows backend from0.1.175 remains without UI or automatic session
creation. Existing FAN/CPU/keyboard behaviour and geometry unchanged. Linux
remains explicit unsupported transport. Full operation/lifecycle mechanism and
physical-validation limits are documented in docs/LIQUID-COOLING.md.

Build-All complete for Windows/Linux, both working portable folders updated.
Four GUI/CLI hashes and versions match build outputs; all application inputs
match current source and377 personal settings hashes are preserved. See
VERIFICATION-0.1.176.json. OEM source/IL/sw provenance: EVIDENCE.json. No tests,
app/CLI/OEM/EC/BLE execution by assistant, no physical response claims. Compiler
warnings present; no errors. No GitHub packaging/publication/push.
