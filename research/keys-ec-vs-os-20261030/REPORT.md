# Wi-Fi, Bluetooth, touchpad and microphone: EC versus OS

30 October2026. Scope: saved exact Wujie OEM5.57 source, its native helper,
the EC update payload embedded in the owner's32MiB BIOS, saved EC RAM29October,
and the matched Wujie DSDT. No live EC/ACPI/HID requests, hardware writes, GUI
or OEM execution. No product change, new version or build. SHA/provenance and
bounded decoded sites are preserved beside this report.

## Result matrix

| Function | OEM implementation actually found | Firmware/hardware evidence | Conclusion |
|---|---|---|---|
|Wi-Fi|DeviceSwitchItem.WIFISwitch -> Windows Radio.SetStateAsync|47B.7 has a native worker leading to real1608.4 output writes; support/pending/power gates exist|Native EC candidate exists; physical pin/module effect and complete safe host request are not verified|
|Bluetooth|DeviceSwitchItem.BTSwitch -> Windows Radio.SetStateAsync|47B.5 template path exists, but its two final handlers in this payload are RET; saved45A.5 is clear|No working independent EC disable operation established for this Wujie|
|Touchpad|Exact Wujie TOUCHPAD_ON/OFF calls SetLCWMI("SetTachpadStatus",1/0); older OEM reference uses Win+Ctrl+F24|Native helper exports03/07 getter and03/08 setter;7A6.3 is used as LED state,7A6.6 gates the touchpad-toggle callback; actual DSDT declares I2C UNIW0001|Firmware API candidate exists, but applicability/semantics on the current BIOS are not proven; direct Precision Touchpad HID is a separate candidate, not raw EC|
|Microphone|MicMuteControl.Trigger -> AudioData.SetInputDeviceMute -> AudioEndpointVolume.Mute|EventB7 asks host software to toggle mute; OEM SetLED changes7A6.2 afterward|Found native request/indicator paths, not a confirmed EC-only audio mute or codec-power gate|

The earlier KEYs classification describes MagicControl0.1.393's current
routes, not a proof that other hardware mechanisms are impossible.

## 1. Wi-Fi: more than an OS-only feature, but not an admitted setter yet

The exact managed Wujie source has WIFISwitch at19095 and the asynchronous
Windows radio setter at18693..18696. The readable reference agrees in
GCService5/DeviceSwitchItem.cs319..335. These paths do not set a raw EC bit.

Separately, the embedded EC contains this trace:

1. Bank1 CODE98C8 begins a gated radio update; worker97BF reads47B.7.
2. Set calls common1AA4; clear calls1AAA.
3. Those trampolines select bank0 and targetB311/B309 respectively.
4. B311 executes `XDATA[1608] |=10`;B309 executes `XDATA[1608] &=EF`.

This is a real native output-register path, rather than two empty functions.
Its physical board net is not identified by the binary alone. Calling it
WLAN power/RF-disable for the actual installed module remains a hardware
hypothesis pending schematic or physical validation.

The full path is conditional:45A.0/.7,97A.7,9124.7, settling67D and power-state
checks participate. A plain47B write is not a demonstrated complete command.
The hotkey pathA177 also checks APP741.0: with an application present it queues
eventA4 instead of simply using the autonomous toggle path. OEM WMIEC handles
A4 through AirplaneModeCtrl/Windows-side radio behavior. Event emission is not
proof that the EC independently disabled a device.

The saved29October coherent/valid RAM reads45A=93 and47B=80 in both before and
after files.45A.0/.7 are set in that snapshot. It is not a current live snapshot
or proof that an experimental write would be safe/effective. See
saved-device-flags.json and device-output-consumers.asm.

## 2. Bluetooth: same template labels do not imply the same implementation

The exact managed source's asynchronous BT setter calls BTModel.SetStateAsync
at18593..18594. Its47B.5 native worker97EA calls common1AB0/1AB6, which select
bank0 B225/B224. Both bytes at those function entries are22 (`RET`). No GPIO
write is performed by these two particular setters.

Saved45A=93 also means its bit5 gate is not set. Another computed-pointer,
SMM, host USB or shared-radio mechanism is not excluded by this bounded review;
these facts do not establish a usable independent EC Bluetooth setter.

## 3. Touchpad: three different meanings must be kept separate

Exact Wujie5.57 MySettingManager handles TOUCHPAD_ON at74544..74547 and OFF
at74568..74571, updates its client preference and invokes the native helper
via SetLCWMI("SetTachpadStatus",1/0). SetLCWMI48547..48566 constructs a helper
command line and starts it; the helper was NOT executed in this analysis.

The exact49,312-byte native helper SHA b90a4416... has these exports:

- GetTachpadStatus RVA2530: a64-byte OEM keyboard WMI request begins03 07
  (instruction140002600 writes word0703 little-endian).
- SetTachpadStatus RVA2750: request begins03 08 bool
  (14000282C writes word0803;140002831 writes bool at+2).

They use the same reviewed OEM keyboard WMI transport family as Copilot,
not SendInput in these exported function bodies. The full underlying BIOS
consumer of command03/08 was not recovered here. The API name is not proof
that it changes the desired physical touchpad state on this particular BIOS.
The earlier read-only Copilot audit did not find OemWMIMethod on this Wujie;
availability was not re-probed today. Another board's working API is not
permission to copy its EC address.

Other EC fields have different meanings:

- OEM MyEcCtrl.GetTouchPadLedStatusFromEC and UserSetTouchPadLedStatus read/RMW
  1958decimal=7A6,bit3. They are LED/state-indication methods, not established
  complete device disable operations.
- In this EC,7A6.6 is tested in bank2BCBA before the touchpad-toggle callback.
  The pinned Linux driver names it TOUCHPAD_TOGGLE_OFF and exposes
  touchpad_toggle_enable. Disabling a hotkey toggle is not disabling the device.
- The actual Wujie DSDT declares I2CB.TPAD with HID UNIW0001 and CID PNP0C50.
  A Precision Touchpad can accept a Selective Reporting feature report to
  stop contact/button reporting. This addresses the touchpad controller,
  not the EC register window. A compatible report descriptor/report ID must
  be obtained from the actual device before admitting that path.

Microsoft specifies Surface Switch57 and Button Switch58: both0 suppress
input, both1 allow contacts/buttons. That state is reset by power reset, so
lifecycle handling would be necessary; a vendor report ID must not be guessed.
The saved pinned dumingqiao helper parses the HID descriptor instead of using
a universal fixed ID. The direct HID path was not exercised on the owner device.

## 4. Microphone: exact OEM shows OS mute and an EC indicator

Exact MicMuteControl.Trigger39521..39526 reads the input mute state, inverts
it and calls AudioData.SetInputDeviceMute. At12880..12890 that method enumerates
capture endpoints and sets AudioEndpointVolume.Mute. GetInputDeviceMute12908
reads the selected input endpoint. WMIEC event183/B7 calls Trigger and OSD.

The separate SetLED39530..39538 RMWs7A6.2. It is invoked from the audio mute
notification as well as initialization. Thus the verified direction is
Windows audio state -> EC indicator. A method/flag that independently mutes
the audio codec or powers off the integrated microphone was not established.
Presence of this common OEM handler also does not prove the owner's keyboard
has a dedicated microphone key or an installed microphone LED.

## ACPI and source limitations

The matched DSDT declares legacy EmbeddedControl offset7B fields TOPD,BLTH,
WLAN and a whole-byte DEVS alias. The declarations have no additional consumer
in the reviewed DSL set. They establish field names/layout, not writable
actuation semantics; neither should be mistaken for raw7A6 or blindly written.
The payload includes valid generic/stub paths. It is the embedded update core,
not an independent dump of the currently running EC flash. None of the four
new native candidates was physically validated or enabled in MagicControl.

## Primary supplementary sources

- Microsoft Precision Touchpad Configuration Collection:
  https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-configuration-collection
- Local pinned dumingqiao/uniwill-laptop-dkms a8f80174aacb, source archive SHA
  b1c9627b989274de429e8a399b5198978377f4ce76ad9cd7481728e4eed2eb60.
  The pinned source was read locally; a web fetch of that exact GitHub blob
  returned a cache miss, not an independent live verification of the URL.

Next useful read-only work: establish current OEM WMI availability and the
03/07 getter; collect actual touchpad HID report descriptor; recover the full
WLAN request/ack path and board-net identity. No test writes are proposed as
part of this analysis, and no API/driver/security settings were changed.
