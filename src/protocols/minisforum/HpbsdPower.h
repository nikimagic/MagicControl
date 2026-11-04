#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include "MagicControl/Performance.h"
namespace mc::hpbsd {
// Common Custom REQUEST range. This is not a claim that the hardware can
// deliver255W: its existing SMU ACK/readback/rollback must still confirm it.
inline constexpr int customPowerMaximum=kApuManualMaximum;
// Application presets, not additional BIOS modes. Native FAN drafts are
// captured independently; a16-point Uniwill CSV is never converted to HPBSD.
inline constexpr std::array<std::array<int,3>,3> customPowerRecipes{{{20,25,30},{40,45,54},{54,60,65}}};
inline std::array<int,3> customPowerMilliwatts(std::array<int,3> values){
    for(auto& v:values){if(v<5||v>customPowerMaximum)throw std::invalid_argument("HPBSD Custom: request outside common Custom bounds");v*=1000;}
    if(values[0]>values[1]||values[1]>values[2])throw std::invalid_argument("HPBSD Custom: STAPM<=Slow<=Fast required");return values;
}
// Exact BIOS1.06 FchTacomaSmmInit/EC61=0C branch. Software profiles use these
// power requests and compiled B1/B2 FAN tables; they do not write Setup/CMOS
// or invoke undocumented SMI commands. Performance still requires live ACK.
inline constexpr std::array<std::array<int,3>,2> stockPowerMilliwatts{{
    {54400,60000,65000},{70800,70800,70800}}};
inline const std::array<int,3>& stockPower(int profile){
    if(profile<0||profile>=static_cast<int>(stockPowerMilliwatts.size()))throw std::invalid_argument("HPBSD: unknown stock power profile");
    return stockPowerMilliwatts[profile];
}
inline std::uint8_t stockFanSelector(int profile){static_cast<void>(stockPower(profile));return static_cast<std::uint8_t>(0xb1+profile);}
inline bool recoverablePower(const std::array<int,3>& value){
    if(value==stockPowerMilliwatts[1])return true;
    return value[0]>=5000&&value[2]<=customPowerMaximum*1000&&value[0]<=value[1]&&value[1]<=value[2];
}
}
