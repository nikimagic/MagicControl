# MACHENIKE L16 / L16 S

Exact SMBIOS MACHENIKE/L16/L16, BIOS N.1.13MAC10, EC1.16; Ryzen9 7945HX,
Radeon610M and RTX5070Ti. Never alias this machine to GM5, an Intel L16 or X5.
Official support: https://www.machenike.com/driver/download/?id=342

## Owner-authorized integration22.10.2026

Owner supplied1.zip: MagicControl0.1.149, dump20261022-145358-0, all4096 EC
bytes valid, reviewed UWACPIDriver10.48.11.331 matches installed SHA256
6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235.
EC Project740=1A is separate from the BIOS enumeration. DefaultTool GetBiosProjctID reads74C=DD;7CA.0=1 selects mask3F, yielding29=ID6. Do not map740 to BIOS_PROJECT_ID.
Capability78E=EC includes RAM1.5 bit6. TablesF00/F30 and commit7C6.2 are present;
F5D/F5E/F5F=C8 are retained observations, not permission to run factory mailbox.
Source evidence and raw arrays: research/l16-1zip-20261022/ANALYSIS.json.

Windows reuses the typed RAM controller with its own exact BIOS/driver/Project
admission. Default, Manual, Max and host Manual online use the shared writer,
masked flags, table readback, ownership and rollback. An untouched L16 online
channel uses its own freshly read table, never another model's factory table.
No exit reset is added. Factory mailbox and the OEM whole-system physical-mode
cycle remain unavailable because this package's factory implementation is
protected and its exact L16 profiles have not been recovered. Explicit FANs and
CPU-only SETs commands are available independently. WMI feedback uses shared OSD.

L16 files: l16-device-settings.ini, l16-power.txt, l16-fan-curves.csv.
The initial editable fan draft uses common application defaults and does not
apply EC state. Old fan-curves.csv and other models' settings are unchanged.

CPU-only SPL/SPPT/FPPT uses native783/784/785, guarded AC and shared AMD codec.
Office/Gaming/Turbo are read fresh from this firmware's734/730/7A7 groups.
Owner dump values are45/45/45,75/85/85,145/145/174. Manual conservative bounds
5..145/145/174 follow this L16's maximum native preset, not GM5's100/100/200.
Economy10/15/20 and Auto0/0/0 reuse shared commands. Readback does not prove
physical SMU enforcement. No GPU clocks/voltage/MUX firmware setters are added.

Battery uses exact DSDT XIF/XST/CYCN, OEM ecBt1Temperature4A2 (SBS0.1K candidate,
raw2990=>25.85C in dump); OS sample supplemented only for one physical battery.
FANs/SETs logger share the reading. Upper7B9 low7 can be configured100(raw0)/95/85
and saved/restored in the L16 file. Preserve platform6, ROMFF,7A6 and reached-bit.
7D0/7D1 are nativeDBD1/DBD2, not established restart percentages: no lower write.
BatteryProtection2 uses the shared native0/1/2 policy API, independently of upper.
The platform4/7+ROM04 switch is not admitted for L16; an imported preference fails
explicitly rather than changing platform6. Physical percentage cutoff remains
unmeasured on this laptop; the supplied dump shows100%/idle, not a limit test.

Backlight reuses existing single-zone RGB/effects/brightness0..4 and checked
transactions, including static startup/resume restoration from L16 preferences.
No HID lightbar, EEPROM writes, fake input or new raw sweep is introduced.

Linux receives the common profile/menu/geometry/brightness range and separate
L16 curve filename. Its Windows EC protocol does not grant a Linux writer.

OEM archive5.60.60.17 and decompiled C#/IL/resources remain in
research/new-l16-20261022. GCUService1.2.0.0 protected Runtime exception stubs
are not recovered bodies; usable DefaultTool and constants retain source limits.
The common paths are connected by explicit owner request, not claimed as tested
physical controls. No live EC/GUI actions are performed by this integration build.