#pragma once
#include "MagicControl/KeysLifecycle.h"
namespace mc::windows {
struct KeysHardwareContext;
// Native firmware adapter only. Invoked through KEYs on its existing worker;
// no hook, thread, watchdog, GUI callback or software keyboard suppression.
KeyObservation observeCopilotFirmware(const KeysHardwareContext&);
void applyCopilotFirmware(const KeysHardwareContext&,bool locked);
}
