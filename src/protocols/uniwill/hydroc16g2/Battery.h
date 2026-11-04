#pragma once
#include <cstdint>
namespace mc::hydroc16g2 {
// Exact recovered GCUService5.23.51.56 BatteryProtection2:585-635,
// MethodDef060009D4/060009D6. Private percentage codec, not named modes0/1/2.
// Explicit owner trial only. Readback does NOT prove charge cutoff.
inline constexpr std::uint16_t batteryUpper=0x7B9,batteryRestart=0x7D0;
inline constexpr std::uint8_t batteryThresholdMask=0x7F;
// Exact recovered MyControlCenter.BatteryInfo.GetECBatteryCycleCount:77-83.
// Read-only little endian word, not a transplanted GM6 capacity/health map.
inline constexpr std::uint16_t batteryCycles=0x4A6;
// Exact recovered MyEcCtrl:171-197: round=7CC&1, TypeC=7CC&0x36,
// reported TypeC rating=max(7CD,7CE). No physical port number/priority proven.
inline constexpr std::uint16_t chargerStatus=0x7CC,typeCPowerA=0x7CD,typeCPowerB=0x7CE;
inline constexpr std::uint8_t roundSocketMask=1,typeCMask=0x36;
}
