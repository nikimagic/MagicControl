#pragma once
#include <cstdint>
namespace mc::uniwill {
// Exact recovered BatteryProtection2 in GM6 and HYDROC OEM services.
// Values0/1/2 are named voltage-protection policies, not charge percentages.
// Do not extend this map to another Uniwill board without its native evidence.
inline constexpr std::uint16_t batteryProtection=0x7A6;
inline constexpr std::uint8_t batteryProtectionMask=0x30;
}
