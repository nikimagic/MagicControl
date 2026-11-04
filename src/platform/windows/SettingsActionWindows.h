#pragma once
#include "BatterySettingsWindows.h"
#include "SettingsPersistenceWindows.h"
#include "LightingControlWindows.h"
#include "CustomModeWindows.h"
#include "EcLoggerWindows.h"
#include "MagicControl/CustomPresets.h"
#include "MagicControl/ResourceControl.h"
#include <functional>
#include <string_view>

namespace mc::windows {
enum class SettingsActionKind {
    BatteryUpper, BatteryRestart, BatteryPolicy, BatteryVoltage, BatteryAging, UsbPowerPriority, StockPower,
    LightingLevel, LightingEffect, LightingColor, LightingCustomColor,
    LightingNative, LightingTimeout, LightingCycle, LightingZone, CustomPreset, CustomEditor, EcLogger, BatteryView, CustomBase
};
enum class LightingCycleIntent {Interval,Effect,Color};
struct SettingsAction {
    SettingsActionKind kind;int value{};bool enabled{};
    BatteryEcPolicy policy{BatteryEcPolicy::Platform4Original};
    KeyboardColor color{};
    KeyboardCycleSettings cycle;
    LightingConfiguration zone;
    std::optional<gui::EcLoggerRequest> logger;
    std::optional<CustomEditorPlan> customPlan;
    PowerOptions powerOptions;
    LightingCycleIntent cycleIntent{LightingCycleIntent::Interval};
    static SettingsAction cycleRequest(KeyboardCycleSettings value,LightingCycleIntent intent=LightingCycleIntent::Interval){SettingsAction r{SettingsActionKind::LightingCycle};r.cycle=std::move(value);r.cycleIntent=intent;return r;}
    static SettingsAction zoneRequest(LightingConfiguration value){SettingsAction r{SettingsActionKind::LightingZone};r.zone=std::move(value);return r;}
    static SettingsAction colorRequest(KeyboardColor value){SettingsAction r{SettingsActionKind::LightingCustomColor};r.color=value;return r;}
    static SettingsAction loggerRequest(gui::EcLoggerRequest value){SettingsAction r{SettingsActionKind::EcLogger};r.logger=std::move(value);return r;}

};
struct SettingsCommandSemantics {
    gui::SettingsRememberChoice remember{gui::SettingsRememberChoice::Ignore};int rememberIndex{};
    bool quickProfile{},nativeBatteryMode{},batteryUpper{},batteryPolicy{},cycleTrigger{},colorChoice{},lightingChoice{};
};
SettingsCommandSemantics settingsActionSemantics(const std::optional<SettingsAction>&,bool gmNativePower,bool passive) noexcept;
// Contexts are acquired lazily on the existing worker thread. The caller owns
// their referenced backends and reservation; each backend keeps its own guards.
struct SettingsActionServices {
    std::function<BatterySettingsContext()> battery;
    std::function<gui::LightingControlContext()> lighting;
    std::function<CustomModeContext()> custom;
    bool* loggerTouched{};
    std::optional<EcByteObservation>* loggerObservation{};
    std::function<void(std::string)> log;
};
struct SettingsActionResult {std::optional<std::array<int,3>> customValues;bool confirmGm{};std::optional<std::array<int,3>> gmExpected;std::optional<int> batteryView;};
// Translates UI intent to an operation on the active resource; this is not an
// admission decision and never selects a model-specific hardware backend.
ResourceControlKey settingsResourceControlKey(const SettingsAction&);
bool settingsResourceRouteRegistered(const ResourceControlKey&);
SettingsActionResult executeSettingsAction(const SettingsAction&,const SettingsActionServices&);
}
