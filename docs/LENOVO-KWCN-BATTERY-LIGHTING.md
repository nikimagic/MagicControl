# Lenovo Legion KWCN: battery and keyboard evidence

Scope: LNVNB161216, 82WK, KWCN54WW. This is a static analysis of the
user-provided EC/BIOS and Lenovo software, not an on-device validation.
Different Legion models and BIOS versions need independent admission.

Battery diagnosis: the existing exact-model read-only collector records raw
C4C1 (B1ST), signed C4D0 (B1CR) and C4D2 (B1AC), C488 (FBFG bit 5),
C250, C252, C4A3, C4BE and C200 alongside EC flow and signed-current direction.
The C250/C252/C4A3 roles are raw candidates from the EC current-mux branch;
their semantic meaning is not established. Current values use the existing
10-second/SOC journal cadence; the snapshot is not atomic and BMS freshness is
unknown. C4D2 is a firmware-selected value, not asserted to be an average.
The card distinguishes EC status from current direction without changing
charge control, Windows observations or firmware flags.

The EC SBS cache maps C214 to temperature (0.1 K), C4F4 to manufacture
date, C4C6 to voltage, C4D0 to signed current, C212 to cycles, C4CA/C4CC
to design/full capacity and C28E to remaining capacity. C22C bit 15 selects
10 mWh units rather than mAh. C4CE holds state of charge; C4C1 holds flow.
C4D8 and C4F6 are BMS requested charge current and voltage, not measured
terminal values. The cache does not expose the freshness of the BMS sample;
individual stable reads do not make the whole snapshot atomic.

C40A bit 4 and C48C bit 0 describe Conservation and Rapid Charge; C48C bit
7 indicates Rapid capability. OEM firmware GBMD reads mode and SBMC writes
it. Lenovo Legion Toolkit uses EnergyDrv IOCTL 0x831020F8, mutually
disables Conservation and Rapid before selecting the other. Firmware
analysis finds 75/80 percent restart/stop in one autonomous branch; other
branches and physical behavior remain unverified. No arbitrary percentage
limits should be asserted from these modes.

The Lenovo Energy package is 15.11.29.70, vendor installer SHA-256
884163897dd3a8315f1984aa14862421f59e96e9ab7e11bb088124c7aa00989f;
the client log reports the same AcpiVpc.sys hash as the official package.
The signed driver maps battery calls to GBMD/SBMC and keyboard calls to
KBLC. Its keyboard IOCTL 0x83102144 exposes off/low/high; the owner's machine
has a white-only keyboard, reported as 0/50/100 percent. LenovoLighting and
LED.sys are separate interfaces and are not required to establish this path.
Windows uses the common battery and lighting resource operations with an exact
KWCN54WW adapter. It admits only the reviewed installed AcpiVpc.sys hash, reads
back each change and keeps Normal, Conservation and Rapid separate from
percentage limits. The white keyboard uses levels 0/1/2. Confirmed choices
are stored in settings/lenovo-energy-settings.ini and replayed through the
same core operations on startup/resume. Another program may change the state;
readback is not proof of long-term ownership. Physical behavior has not yet
been tested on the customer's laptop.

INFO displays the version from the installed service SYS file, not a fixed
version and not an in-memory driver-version query. Only the actual supported
Lenovo exposes the installer button when the service is missing. The official
installer is embedded, signature/hash checked and started interactively only
on explicit request. An installed service is never overwritten automatically.
Linux remains read-only/unconnected for these Lenovo Energy controls.

Evidence: Lenovo Energy Management download
https://download.lenovo.com/consumer/mobiles/wwe00mae40.exe and upstream
Lenovo Legion Toolkit implementation
https://github.com/BartoszCichecki/LenovoLegionToolkit .
