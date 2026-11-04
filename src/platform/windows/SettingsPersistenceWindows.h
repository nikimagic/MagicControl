#pragma once
#include "MagicControl/Performance.h"
#include "MagicControl/PortableSettings.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace mc::windows {
// Called only after the existing device operation has completed and its
// fresh observation has been accepted; these helpers never write hardware.
bool loadPowerStartupSettings(bool allowed,DevicePreferences& saved,std::string& error);
void rememberObservedPower(const std::filesystem::path&,bool wujie,int preset,
                           bool manual,std::uint8_t mode,const EcPowerLimits& overrides);
}
namespace mc::gui {
struct SettingsSnapshot;
struct LightingControlContext;
enum class SettingsRememberChoice { Ignore, LightingLevel, LightingEffect, LightingColor,
    LightingCustomColor, LightingNative, LightingCycle, Performance, ManualPower };
struct SettingsRememberContext {
    const SettingsSnapshot& actual;
    SettingsRememberChoice choice{SettingsRememberChoice::Ignore};
    int index{},command{};
    bool hidLighting{},oem{},nativePower{},nativeBattery{};
    std::function<LightingControlContext()> lightingContext;
    std::function<bool()> cycleActive;
    std::function<std::filesystem::path()> preferences;
    std::function<bool()> wujiePower;
    std::function<void(std::string)> log;
};
// Saves only already observed settings. This function never sends an EC or
// firmware command and preserves backend-specific persistence ownership.
void rememberSettingsChoice(const SettingsRememberContext&);
}
