#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace mc {
// Saved ChargeAuto values0/1 keep their existing meanings.2 is an explicit
// platform7/original-ROM choice, not evidence of a universal factory default.
enum class BatteryEcPolicy {Platform4Original=0,Platform7Percent=1,Platform7Original=2};
inline std::optional<BatteryEcPolicy> batteryEcPolicy(int value) noexcept {
    if(value<0||value>2)return {};
    return static_cast<BatteryEcPolicy>(value);
}
inline constexpr bool nativeEcPolicyProfile(std::string_view id) noexcept {
    return id=="wu-jie-15-xpro"||id=="mechrevo-gm6ir0c"||id=="mechrevo-gm5hg0a"||id=="eluktronics-hydroc16g2";
}
// Owner31.10.2026: percentage commands preserve the observed platform. P7
// uses owned RAM ROM4 for a limit, originalFF for100/native. This is intent
// selection only, never permission to write an unreviewed model or ROM byte.
inline std::optional<BatteryEcPolicy> batteryPolicyForChargeLimit(unsigned platform,int upper) noexcept {
    if(upper<1||upper>100)return {};
    if(platform==7)return upper==100?BatteryEcPolicy::Platform7Original:BatteryEcPolicy::Platform7Percent;
    if(platform==4)return BatteryEcPolicy::Platform4Original;
    return {};
}
inline constexpr std::uint8_t batteryPolicyPlatform(BatteryEcPolicy p) noexcept {return p==BatteryEcPolicy::Platform4Original?4:7;}
inline constexpr std::uint8_t batteryPolicyRom(BatteryEcPolicy p) noexcept {return p==BatteryEcPolicy::Platform7Percent?4:255;}
inline std::optional<BatteryEcPolicy> observedBatteryEcPolicy(std::uint8_t platform,std::uint8_t rom) noexcept {
    if(platform==4&&rom==255)return BatteryEcPolicy::Platform4Original;
    if(platform==7&&rom==4)return BatteryEcPolicy::Platform7Percent;
    if(platform==7&&rom==255)return BatteryEcPolicy::Platform7Original;
    return {};
}
}
