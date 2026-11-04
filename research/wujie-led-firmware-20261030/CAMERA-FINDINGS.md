# Wujie camera and indicator: read-only findings

30 October 2026. No image/audio capture, streaming, device reset, driver
installation, EC write or camera control SET was performed. Product unchanged.

## Confirmed on the current machine

`pnputil /enum-devices /class Camera /connected /deviceids /drivers` reports:

| Interface | Friendly name | Driver |
|---|---|---|
| USB VID_2B7E PID_B663 REV_0008 MI_00 | HD Webcam | usbvideo.inf |
| USB VID_2B7E PID_B663 REV_0008 MI_02 | IR Camera | usbvideo.inf |

The IR interface reports parent `USB\VID_2B7E&PID_B663\01.00.00`, sibling
MI_00, service `usbvideo`, Microsoft driver 10.0.26100.9818. Reported stack:
`ksthunk`, `usbvideo`, `ACPI`, `usbccgp`; LowerFilters: WdmCompanionFilter.
BIOS device name: `\_SB.PCI0.GPPA.XHC1.RHUB.PRT1.CAM1`.
Raw PnP outputs are saved beside this report. Some localized strings in native
console output have an encoding mismatch; ASCII IDs/names/versions are intact.

Owner observation: the blue physical LED lights when the Windows Camera app
starts displaying video, not just when KEYs enables camera availability.
Microphone mute works, but no physical microphone LED was observed.
Owner reports two microphone apertures, camera, LED window and another optical
window. Aperture assignment cannot be established from software inventory alone.

This establishes a USB UVC RGB/IR module. A public first-hand report for the
same VID/PID identifies SunplusIT Inc and an IR emitter, on a DIFFERENT laptop:
https://github.com/EmixamPP/linux-enable-ir-emitter/issues/246
It corroborates the module family, not the exact board wiring or control bytes.

## Blue, red and blinking are different questions

Microsoft distinguishes a visible camera-in-use indicator from an IR
illuminator. IR illumination can be 850 nm (a faint visible glow may be
perceptible) or 940 nm. A blue activity LED does not establish a multicolor LED.
The wavelength, emitter count and physical layout on this exact machine have
not been measured or established from the module's documentation.

https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/camera-privacy-controls

The standard IRTORCHMODE API can advertise Off, Always on, and Alternating frame
illumination, with power level/range. These are possible IR emitter modes, not
proof that this camera implements them or that the visible blue LED has a
user-selectable blink/color setting. Subsequent live read-only KS queries are
documented below; the generic standard API must not be mistaken for an admitted
command for this module.

https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/ksproperty-cameracontrol-extended-irtorchmode

Microsoft's UVC extension maps IR torch control to an extension unit
MS_CAMERA_CONTROL_XU `{0F3F95DC-2632-4C4E-92C9-A04782F43BC8}`, selector 0x0A.
The subsequently collected descriptor below establishes that the Microsoft
unit is present but selector 0x0A is NOT advertised by this configuration.
Do not send control bytes inferred from a similar camera.

https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/uvc-extensions-1-5

## What is traced and what remains open

The camera's current OS path is USB/UVC through Microsoft's usbvideo driver;
this is independent evidence from generic AMD ISP camera code in the BIOS.
Windows Camera starts capture; the owner's activity LED follows that capture.
The exact USB request, internal camera firmware GPIO and any involvement of an
additional board signal have NOT been traced. Do not claim that BIOS/EC cannot
affect it, or that there is a standalone blue LED setter.

Product `WindowsDevices.cpp` uses EC 0x727 mask 0x18 for camera availability;
that is not established as an activity-LED command. OEM microphone SetLED uses
0x7A6 bit 2; it is not a camera LED or IR emitter control.

Saved EC direct-reference analysis is in ec-7a6-windows.asm and
ec-literal-references.json. Seven direct 0x7A6 sites manipulate/test other bits;
no direct bit-2 consumer has been found in those sites. Computed accesses and
external controllers remain a limitation of that negative result.

The platform BIOS image is not automatically a dump of the USB camera
controller's own firmware.

## Live USB/KS follow-up, 30 October 2026, 09:49-09:53 Moscow

Read-CameraUsbDescriptors.ps1 successfully retrieved 57 connection-information
bytes and the complete 1001-byte configuration descriptor from the exact parent
hub/port. It verifies VID/PID and connected status before GET_DESCRIPTOR. Hub
handle access follows Microsoft's USBView GENERIC_WRITE convention; the only
issued IOCTLs were connection information and GET_DESCRIPTOR, not USB writes.
An earlier zero-access hub-open attempt returned Access Denied. The successful
descriptor and subsequent KS reads ran outside the tool sandbox, without a
camera stream or SET request. Raw bytes, timestamps and decoded descriptors are
saved beside this report.

| Camera interface | Hardware XU unit | GUID | Advertised selectors |
|---|---|---|---|
| RGB MI_00 | 3 | 0fb885c3-68c2-4547-90f7-8f47579d95fc | 1-5 |
| RGB MI_00 | 4 | 63610682-5070-49ab-b8cc-b3855e8d221d | 1-17,21-23,30 |
| IR MI_02 | 7 | 0f3f95dc-2632-4c4e-92c9-a04782f43bc8 | 6-9 |
| IR MI_02 | 4 | 63610682-5070-49ab-b8cc-b3855e8d221d | 1-23,25-30 |

Both video functions advertise UVC 1.50. RGB streaming endpoint is 0x81 with
isochronous alternate settings; IR endpoint is 0x82, also isochronous. Neither
was started. This identifies the streaming transport, not the LED GPIO.

Read-CameraKsControls.ps1 only allows property GET, topology GET and
BASICSUPPORT flags. It creates no pins/graph and does not start capture. The
RGB capture interface opens normally. IR requires its ENABLED SensorCamera
category 24e552d7-6523-47f7-a647-d3465bf1f5ca; its Capture-category path is
disabled. Using the actual enabled category returned topology and vendor XU
properties without changing registration or installing another driver.

Hardware XU unit IDs are NOT KS node IDs. Both functions currently report five
KS nodes. RGB Sunplus unit3 maps to KS node2 and vendor unit4 to node3; IR vendor
unit4 maps to node3. The Microsoft IR unit is not returned by the generic
extension-info queries made here; its descriptor was obtained from USB.

Selected GET results before any owner-requested comparison session:

| Interface / XU GUID prefix | Selector | Returned bytes | Current | Basic-support range |
|---|---|---|---|---|
| RGB / 0fb885c3 | 1 | 9 | 28 10 01 00 1F 00 00 00 FF | raw 9-byte fields, not decoded |
| RGB / 0fb885c3 | 2 | 1 | 01 | 0-6, step1 |
| RGB / 0fb885c3 | 3 | 1 | 01 | 1-4, step1 |
| RGB / 0fb885c3 | 4 | 2 | 08 00 | 4-8, step4 |
| RGB and IR / 63610682 | 1 | 1 | 01 | read-only, 0-1 |
| RGB and IR / 63610682 | 2 | 2 | 00 20 | 0-65535, step1 |
| RGB and IR / 63610682 | 13 | 4 | 00 00 00 00 | 0-0xFFFFFF, step1 |

Sunplus RGB selector5 is advertised but returns Win32 1168 through KS. All
results, including failures and exact basic-support blobs, are preserved in
camera-ks-baseline.json. These are unidentified values, NOT named LED settings.
No owner confirmation of physical blue-LED state at this baseline was obtained.

## External control-name leads: checked, not adopted

1. Libusb-UVC calls GUID63610682 selector2 "LED Control" without an exact
   2B7E:B663 board/firmware mapping in the quirk file:
   https://github.com/jeffwitz/Libusb-UVC/blob/06b3bb8eeb1bfe4333b9dba2f78956ff351c2d0e/src/libusb_uvc/quirks/guid_63610682-5070-49ab-b8cc-b3855e8d221d.json
   The owner's selector2 is a 2-byte 0..65535 field, currently 0x2000.
   The name alone is not evidence of LED semantics.

2. A real Sunplus SPCA2089D firmware tool for a DIFFERENT camera (1BCF:28C5)
   uses the SAME vendor GUID: selector2 REG_ADDR (2 bytes), selector3 REG_DATA,
   selector13 ERASE_SECTOR (4 bytes). These widths match the owner's selected
   fields. This is a concrete conflicting interpretation, not permission to
   use the flash protocol on Wujie or proof of its exact chip model:
   https://github.com/Spectrum3847/SpectrumJetson/blob/aa41b6af5e883b49047e0b15791d86d778a2e376/scripts/jetson/thriftiest-fw.py
   NO write to any such selector was attempted.

3. Samsung Odyssey G90XF 04E8:20D7 research reports IR enable at Sunplus GUID
   0fb885c3 selector2 as a bool. The owner's field is instead an enum0..6.
   Matching GUID/selector does not establish compatible semantics:
   https://github.com/m-j-tilley/Linux_3D_SBS_Player/blob/3205262dba46532ceeed7b538138f6a38ab35b62/hub/ir_control.py

4. A first-hand report on another Wujie 15X Pro with the exact USB ID describes
   RGB streaming as a workaround for IR initialization after cold boot, but
   supplies no independent blue LED control:
   https://www.iruanp.com/posts/2026/mechrevo-wujie-15x-pro-gaze-ir-camera-cold-boot-fix/
   It was read through the search tool; no capture/workaround was executed here.

## Current result

Read-only access to actual USB controls on both camera functions works. A
standalone blue LED, color or blink command has NOT been identified. Standard
MS IR-torch selector10 is not advertised; several tempting internet "LED"
names conflict with actual widths and a documented maintenance interface.

A control that stays unchanged across capture does not prove there is no
internal LED gate: automatic streaming logic may drive the pin without a
separate exposed property.

## Owner-started activity comparison, 09:55-09:57 Moscow

The owner answered the request to open Windows Camera. The 09:55:20 sample
found all 36 previously recorded KS result rows unchanged. WindowsCamera
PID22408 was present (start09:55:00). The owner subsequently explicitly
confirmed: the blue LED had been lit; closing the app made it go out.

The next sample at09:56:35 again found all 36 common rows unchanged, including
all successfully read selected current values. WindowsCamera PID22408 still
existed even after the owner closed its window, so process presence alone is
NOT used as evidence of active capture.

The owner additionally reported: minimizing the camera window extinguishes
the LED, restoring the window lights it. A read at09:57:19 after this report
again found all 36 common result rows unchanged. No separate snapshot was
obtained during the minimized interval itself.

Additional read-only KS pin-type/global-instance queries were introduced to
check stream usage without frames. Both functions report two pin types, but
GLOBALCINSTANCES returns Win32 1168 for all queried pins. This diagnostic does
not provide an independent active-stream counter on this driver.

Raw phase snapshots and camera-physical-control-comparison.json preserve the
results and observation limits. The baseline physical LED state was never
explicitly confirmed; the owner-confirmed open/closed pair is stronger evidence.
Diagnostic code never opened a capture pin or requested a video frame, and
never sent SET, stream-start/stop, reset or EC commands.

Interpretation: automatic LED gating associated with sensor capture/stream
activity is consistent with these observations. A dedicated LED setter, the
precise internal GPIO, and whether any vendor command can override that gate
are still unproven. Minimizing a window is not itself a USB LED protocol.

Microsoft's privacy indicator guidance requires visible indication when the
ISP captures sensor data:
https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/camera-privacy-controls

Microsoft's CameraStarterKit sample handles visibility changes and cleans up
capture when hidden. This is an example explaining how such an effect can
arise, not source-code proof of the installed Windows Camera app's behavior:
https://github.com/microsoft/Windows-universal-samples/blob/main/Samples/CameraStarterKit/cs/MainPage.xaml.cs
