#pragma once
#include "MagicControl/KeysLifecycle.h"
#include "WindowsDevices.h"
namespace mc::windows {
struct KeysHardwareContext;
DeviceState readMicrophoneKey(const KeysHardwareContext&);
KeyObservation observeMicrophoneKey(const KeysHardwareContext&);
// One handler for explicit apply/lifecycle and event-driven indication.
// observeOnly never changes Windows mute; it follows a fresh current reading.
void applyMicrophoneKey(const KeysHardwareContext&,bool enabled,bool observeOnly);
}
