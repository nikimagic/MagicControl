#pragma once
#include "../common/BatteryPolicy.h"
namespace mc::gui::settings_controls {
inline constexpr int kBatteryFirst=100,kBatteryAutomatic=kBatteryFirst+ui::batteryAutomaticIndex;
inline constexpr int kPerformanceFirst=110,kEffectFirst=120,kColorFirst=130,kLevelFirst=140;
inline constexpr int kNativeMode=150,kCustomColor=151,kLightingZoneTitle=152;
inline constexpr int kBatteryCardTitle=250,kBatteryVoltageFirst=251;
inline constexpr int kBatteryAgingOff=254,kBatteryAgingOn=255;
inline constexpr int kCycleEdit=220,kCycleSpin=221,kRestartEdit=230,kRestartSpin=231;
inline constexpr int kLoggerAddress=240,kLoggerAddressSpin=241,kLoggerValue=242,kLoggerValueSpin=243;
inline constexpr int kKeyboardTimeoutEdit=170,kKeyboardTimeoutSpin=171,kKeyboardTimeoutCheck=172;
inline constexpr int kKeyboardIdleValue=173,kKeyboardIdleSpin=174;
inline constexpr int kManualPower=180,kCustomPresetFirst=181,kPowerEditFirst=190,kPowerSpinFirst=193;
inline constexpr int kPowerOptionEditFirst=270,kPowerOptionSpinFirst=280;
inline constexpr int kApuPolicyOff=200,kApuPolicyOn=201,kApuPolicyOriginal=202;
inline constexpr int kUsbPriorityBattery=203,kUsbPriorityPerformance=204;
inline constexpr int kCustomBaseZero=205,kCustomBaseTurbo=206;
inline constexpr bool customPresetCommand(int id){return id>=kCustomPresetFirst&&id<kCustomPresetFirst+3;}
}
