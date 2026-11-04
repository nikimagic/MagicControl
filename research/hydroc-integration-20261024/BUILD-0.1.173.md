# MagicControl0.1.173

Common Build-All succeeded. Windows/Linux GUI and CLI portable binaries match
build outputs by SHA256; generated and portable versions agree. See
VERIFICATION-0.1.173.json. All494 recorded application inputs match current
sources;376 personal settings files retain their original hashes.

LightingZones provides one compact card/title selector and independent zone
configuration. Existing keyboard core, Huawei backlight, shared Geometry and
NumericFieldTheme remain unchanged (unchanged-components.json). Shared title
button uses existing ControlTheme/PopupTypography with an opt-in alignment;
ordinary buttons keep their defaults. Linux has no admitted HID writer and
therefore exposes no invented extra zones.

HYDROC uses own BIOS/SYS/project/P13/LC guards for the shared RAM1.5 FAN/CPU
controller; PL4 Double uses its own live flag/ceiling. Diagnostic factory requests
use Project26 and the same controller/online lifecycle. Battery semantics are
unchanged; no GM6 battery decoder or SingleZone EC lighting is transplanted.

ITE is exact HYDROC only. Current keyboard admission requires FF03/usage1,
FWmajor34 and EC73C24; other keyboard generations remain unavailable until their
calibration/static-map codec is implemented. Lightbar4 requires unique7000/7001
FF03/usage2 plus OEM fitted-board Force flags for each exposed zone. Native
runtime08/14 uses descriptor feature size and Save0; no global zone-off,1A,
welcome/UEFI/flash writes. Four firmware effects plus speed/direction are exposed;
OEM music, letters and host/per-key animations are not claimed implemented.

Each zone saves its own configuration through common atomic storage and is
included in dump settings. Viewing a zone sends no command. HID acceptance is
reported as acceptance/configuration saved, never full RGB/physical readback.
No startup/resume replay of unknown HID state. The initial Default is our explicit
white/Mono/level4 configuration, not an asserted recovered OEM factory colour.

Original0.1.69 HYDROC archive inspected offline; full EC and pinned driver facts
retained in dump-evidence.json. HID VID/PID/caps/FW/Force flags are absent from
that old archive. GUI, CLI, OEM programs, EC and HID were not executed/opened by
the assistant. No tests or physical probes were added/run. Physical HYDROC
response and a live visual check remain unmeasured. No GitHub package/push.
