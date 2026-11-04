#pragma once
#include <array>
#include "MagicControl/CustomPresets.h"
#include <cstdint>
#include "../BatteryProtection.h"
// GM6IR0C / GCUService1.0.2.70, MyFanManager_RamFan1p5 and
// FanTable_Manager1p5/MyFanTableCtrl. Source-backed, pending laptop validation.
// Shared bytes always use masked RMW. Readback is NOT proof of enforcement.
namespace mc::gm6 {
inline constexpr std::uint16_t project=0x740, capability=0x78E;
inline constexpr std::uint16_t mode=0x751, custom=0x726, light=0x727;
inline constexpr std::uint16_t commit=0x7C6, respective=0x7C5;
inline constexpr std::array<std::uint16_t,3> power{0x783,0x784,0x785}; // Intel PL1/PL2/PL4
// Exact active MyFanManager_RamFan1p5.GetOffice/Gaming/TurboPLDefaultValue:
// read-only firmware defaults, excluding each adjacent GPU D-state byte.
// These are sources for application CPU-only PL presets, NOT permission to
// select the OEM whole-system mode or rewrite its FANs tables/0x751 flags.
inline constexpr std::array<std::array<std::uint16_t,3>,3> powerDefaults{{
    {{0x734,0x735,0x736}}, // Office
    {{0x730,0x731,0x732}}, // Gaming, presented as Balanced
    {{0x7A7,0x7A8,0x7A9}} // Turbo
}};
// Reviewed GM6 OEM Fan/Status ceilings/minima, not guaranteed effective watts.
// Runtime defaults must fit these bounds; never substitute archived wattages.
inline constexpr std::array<int,3> powerMinimum{10,10,10},powerMaximum{140,165,250};
inline constexpr std::array<std::uint16_t,2> table{0xF00,0xF30};
// Exact active GCUService BatteryProtection2.SetHealthProtectionHigh/Middle/Low:
// 00/10/20 select Performance/Balanced/Healthy. Preserve ALL other bits.
// This is independent of fan ownership, PL overrides and percentage 7B9/7D0.
inline constexpr auto batteryProtection=uniwill::batteryProtection;
inline constexpr auto batteryProtectionMask=uniwill::batteryProtectionMask;
// Explicit owner validation20.10.2026, exact BatteryProtection2 private codec
// SetBatteryChargingLimit_Up/Down: low7,100 upper=0, restart80=0x50.
// Readback is configuration only. Never infer activation from Wujie7C3=4.
// Write restart first, preserve both bit7; no automatic restore/persistence.
inline constexpr std::uint16_t batteryUpper=0x7B9,batteryRestart=0x7D0;
inline constexpr std::uint8_t batteryThresholdMask=0x7F;
// Exact MyEcCtrl GetTypeCsatusFromEC/GetRoundSocketStatusFromEC. Raw0 is
// unknown, not disconnected: observed with WindowsAC=true in owner's dump.
inline constexpr std::uint16_t chargerStatus=0x7CC;
inline constexpr std::uint8_t roundSocketMask=1,typeCMask=0x36;

using Point=mc::CustomFanPoint;
using Curve=std::array<Point,16>;
using Curves=std::array<Curve,2>;
}
