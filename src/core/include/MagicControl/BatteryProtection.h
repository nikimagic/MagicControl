#pragma once
#include <optional>
#include <string_view>
#include "BatteryEcPolicy.h"
namespace mc {
// OEM protection policies are not percentage thresholds. Values match the
// native GM6/HYDROC BatteryProtection2 enum; no mapping to 100/95/85 is implied.
inline constexpr bool nativeBatteryProfile(std::string_view id) noexcept {
    return id=="mechrevo-gm6ir0c"||id=="eluktronics-hydroc16g2"||id=="machenike-l16";
}
enum class BatteryProtectionMode { Performance=0, Balanced=1, Healthy=2 };
// Admission for an already observed native context, NOT a factory-default
// discovery API and NEVER a source of platform/ROM write targets. Preserve IDs.
inline bool nativeBatteryVoltageContext(std::string_view id,unsigned platform,unsigned rom) noexcept {
    if(rom!=255)return false;
    if(id=="wu-jie-15-xpro"||id=="mechrevo-gm6ir0c")return platform==7;
    if(id=="eluktronics-hydroc16g2")return platform==13;
    if(id=="machenike-l16")return platform==6;
    return false;
}
// A button may offer the existing owned ROM04 release path. This is not
// permission to write a native voltage profile before ROM FF is confirmed.
inline bool nativeOwnedVoltageTransition(std::string_view id,unsigned platform,unsigned rom,bool owned) noexcept {
    return platform==7&&rom==4&&owned&&nativeEcPolicyProfile(id)&&nativeBatteryVoltageContext(id,7,255);
}
inline std::optional<BatteryProtectionMode> batteryProtectionMode(int value) noexcept {
    if(value<0||value>2)return {};
    return static_cast<BatteryProtectionMode>(value);
}
}
