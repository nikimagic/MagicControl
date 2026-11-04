#pragma once
#include "MagicControl/DisplayContinuity.h"

namespace mc::linux_platform {
// A worker may fail after issuing a command. Preserve the last display values,
// but revoke backend writability until a new observation. This transition does
// not access hardware, change settings or schedule another operation.
inline void invalidateSettingsAfterWorkerFailure(LaptopDisplayCache& cache,
    LaptopControlSnapshot& laptop,BatteryReading& reading,BatteryReading& osBattery,
    EcBatteryReading& ecBattery) {
    laptop=cache.update({});
    reading.dynamicStale=true;osBattery.dynamicStale=true;
    reading.healthStale=reading.healthPercent.has_value();
    osBattery.healthStale=osBattery.healthPercent.has_value();
    const bool hadEc=ecBattery.coherent||ecBattery.retainedForDisplay;
    ecBattery.coherent=false;ecBattery.retainedForDisplay=hadEc;
    ecBattery.battery.dynamicStale=hadEc;
}
}
