#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

namespace mc::gm6 {
// Active OEM OperatingMode, not the CPU-only preset or FANs editor selection.
enum class NativeProfile : int { Office=0, Gaming=1, Turbo=2, Custom=3 };
inline constexpr std::string_view nativeProfileName(NativeProfile mode) {
    switch(mode){case NativeProfile::Office:return "Office";case NativeProfile::Gaming:return "Gaming";
        case NativeProfile::Turbo:return "Turbo";case NativeProfile::Custom:return "Custom";}
    return "Unknown";
}
inline constexpr NativeProfile nextNativeProfile(NativeProfile mode,bool customAvailable=true) {
    const auto next=static_cast<NativeProfile>((static_cast<int>(mode)+1)%4);
    return next==NativeProfile::Custom&&!customAvailable?NativeProfile::Office:next;
}
inline constexpr std::uint8_t nativeModeBits(NativeProfile mode) {
    switch(mode){case NativeProfile::Office:return 0xA0;case NativeProfile::Turbo:return 0x10;default:return 0;}
}
struct NativeProfileFlags {
    std::uint8_t mode{},custom{},light{},commit{};
    friend bool operator==(const NativeProfileFlags&,const NativeProfileFlags&)=default;
};
inline constexpr std::optional<NativeProfile> nativeProfile(const NativeProfileFlags& raw) {
    const bool custom=(raw.custom&0x80)!=0,light=(raw.light&0x40)!=0;
    const auto mode=raw.mode&0xB0;
    if(custom||light){
        if(custom&&light&&(raw.commit&4)&&(mode==0||mode==0x10))return NativeProfile::Custom;
        return {}; // Mixed flags never become a guessed cycle index.
    }
    if(mode==0xA0||mode==0x80)return NativeProfile::Office;
    if(mode==0)return NativeProfile::Gaming;
    if(mode==0x10)return NativeProfile::Turbo;
    return {};
}
} // namespace mc::gm6
