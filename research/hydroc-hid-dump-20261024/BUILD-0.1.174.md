# MagicControl 0.1.174

Explicit HYDROC dumps capture lighting-hid.json and the corresponding diagnostic
inventory section. Preserve descriptors, raw80H reply and firmware bytes/hex/
decimal representation; USB bcdDevice remains separate. Capture ForceBase,
ForceLogo, ForceHinge, Forcenewlogosupport and MBlogoSupport with raw DWORD/type,
Win32 status and32/64-bit registry view. Missing/blocked/wrong-type/invalid reply
are distinct from zero or an absent physical LED module.

Reuse native HID enumeration/open/query, common strict JSON parser and existing
bounded metadata child process. Separate HID_VERSION_QUERY scope allows only
fixed80H; no runtime controller/watchdog,08/14/1A effect/save, registry setter or
EC access in the evidence getter. Parent20s, child15s/64 candidates; emit flags,
descriptors and query-begin before I/O so partial rows survive timeout. Guard
exact HYDROC BIOS and descriptor allowlist; active/unknown OEM blocks firmware
query. Propagate --no-ec. OS named mutex serializes feature request/reply with
runtime lighting, with fresh descriptor verification after reopening.

Normal INFO refresh remains metadata-only; explicit full dumps collect this
additional getter. Existing Default/Max dump-mode policy is unchanged. Linux
receives the same build and explicit unavailable evidence, no new HID writer.
Existing keyboard core/card geometry/FAN control implementations unchanged;
see unchanged-components.json. LPP pump/fan BLE control was only inspected in
OEM code, not implemented or invoked in this task.

Build-All completed both OSes and staged both working portable copies. Four
GUI/CLI hashes and versions match build output, application inputs are current,
and all personal settings baseline hashes are preserved. Full details in
VERIFICATION-0.1.174.json. Compiler warnings present; no compilation errors.
No tests, app/CLI/OEM or hardware probes were executed by the assistant. Live
firmware replies are therefore not claimed observed. No GitHub archives/push.
