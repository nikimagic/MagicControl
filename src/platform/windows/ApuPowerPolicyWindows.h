#pragma once
#include <windows.h>
#include <algorithm>
#include "MagicControl/Performance.h"
#include "FanControlWindows.h"
#include "UniwillControlOwnerWindows.h"

namespace mc::windows {
// Windows supply state, not battery Charging/Discharging or an EC priority bit.
// Unknown supply is treated conservatively; Turbo requires confirmed AC.
inline bool apuOnBattery() {
    SYSTEM_POWER_STATUS status{};
    return !GetSystemPowerStatus(&status) || status.ACLineStatus != 1;
}
inline std::array<int, 3> apuAllowedLimits(std::array<int, 3> values) {
    if (apuOnBattery()) for (int i = 0; i < 3; ++i)
        values[i] = std::min(values[i], kApuBatteryMaximum[i]);
    return values;
}
}
