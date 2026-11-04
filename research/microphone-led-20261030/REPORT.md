# Microphone KEYs operation and Wujie mute LED

One MicrophoneKeysWindows resource handler now coordinates the existing
Windows default communications-capture mute and the source-verified Wujie
indicator request. GUI, CLI and ordinary lifecycle use applyKeyHardware;
the old generic WindowsDevices microphone writer now refuses that action,
preventing a second command path that would bypass indication policy.

Explicit On/Off still uses setDefaultMicrophoneEnabled and fresh readback.
Only then is actual mute mirrored to EC7A6.2: enabled ->0,muted ->1. The native
protocol getter/setter verifies actual Wujie82/72/26,awake6E6==1 and writes
only mask04. It preserves battery protection7A6[5:4], touchpad-related fields
and all other bits. No APP/FAN/RAM/PL command is involved. Native support is
not inferred for other profiles, previews, Honor or HPBSD. Linux retains its
existing microphone backend; no unverified Linux EC transport was added.

Missing/paused EC or an optional LED failure does not undo confirmed Windows
mute, unmute the microphone or prevent saving its primary state. The getter
reports OS enabled and the independent LED request flag. A flag readback is
not proof that a physical LED exists or is illuminated on this board.

Windows audio callbacks remain read-only. Their existing UI notification
forwards a coalesced observation to the same KeysLifecycle worker, with no
new thread/watchdog/timer or GUI hardware I/O. Observe intent of the SAME
resource handler never invokes the Windows mute setter: it re-reads current
capture state and only mirrors the current mute to the indicator. That avoids
replaying an old bool from a delayed notification. The generic lifecycle then
freshly reads and persists the observed choice. Its epoch/admission/suspend
rules and restoration priority remain in force; own callback echoes coalesce
and matching state does not cause a second LED data write.

The indicator follows external mute while MagicControl is running. Closing
the program leaves the last native flag, not a background service. Default
capture selection/stability semantics are the existing microphone API's;
this feature does not claim an EC privacy gate or mute of every capture device.

## Validation scope

The common Windows/Linux GUI+CLI build and read-only status verification are
recorded in verification.json. The read collector uses no value argument,
setter, lifecycle, GUI launch or --yes; it also checks --no-ec status to ensure
the system microphone remains readable without the native indicator backend.

No automated test suite was added/run per AGENTS.md. Physical mute/LED and
sleep-cycle behavior still require the owner trial; compiled code and a
native flag are not presented as a physical LED test. Live user settings,
old saved key names and other devices' handlers are preserved. No LED INI
or duplicate preference was introduced. No GitHub packaging/publication.

## Owner trial on0.1.394

The owner confirmed: microphone switching works, but no physical LED is
visible. GUI session06:19:56UTC/91611434659100 reports394. Repeated Off/On
actions between09:20:18 and09:21:01 Moscow write7A6=04/00 and confirm native
readback. The observed-state worker also followed the change without replaying
an old saved microphone setting. Selected events and the exact answer are in
physical-validation.json. This validates mute and the native request flag,
not an installed/illuminated LED. No other bits were tried and no sleep trial
was performed. These trial findings supersede the pending-validation note above.

The owner separately observed a BLUE camera LED only after starting the camera
image, not merely enabling it in KEYs. This is a camera-capture observation,
not a microphone LED, RGB capability or an independent EC LED setter. The
observation is preserved in camera-led-observation.json; no camera command
or indicator control was added in this task.
