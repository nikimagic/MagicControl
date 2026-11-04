#pragma once
#include "MagicControl/Performance.h"

namespace mc::x5sp4nag {
inline bool overridesCleared(const EcPowerLimits& v){
    return v.sustained&&v.shortBoost&&v.peak&&*v.sustained==0&&*v.shortBoost==0&&*v.peak==0;
}
inline int nativePowerButton(std::uint8_t mode){
    switch(mode&0x90){case 0x80:return 1;case 0:return 2;case 0x10:return 3;default:return -1;}
}
inline int savedPowerButton(std::optional<int> legacyMode,bool manual,bool automatic){
    if(automatic)return 4;
    if(legacyMode)return *legacyMode==4?5:*legacyMode;
    return manual?5:-1;
}
// Explicit Auto intent + fresh zero readback, as GM6; legacy Custom stays separate.
inline int powerButton(const EcPerformanceState& state,bool automatic,bool custom){
    const auto& v=state.overrides;
    if(!v.sustained||!v.shortBoost||!v.peak)return -1;
    if(overridesCleared(v))return automatic?4:nativePowerButton(state.rawMode);
    if(custom)return 5;
    const std::array<int,3> values{*v.sustained,*v.shortBoost,*v.peak};
    for(int i=0;i<4;++i)if(values==kApuPowerPresets[i])return i;
    return 5;
}
inline EcPerformanceState powerState(std::uint8_t mode,const std::array<EcPowerLimits,3>& presets,
                                    const std::array<std::uint8_t,3>& rawOverrides){
    EcPerformanceState state;state.rawMode=mode;state.presets=presets;
    const int selected=nativePowerButton(mode)-1;
    if(selected>=0)state.active=presets[selected];
    // Every raw byte was successfully read. Zero is confirmed firmware policy,
    // not an unavailable reading and not a zero-watt active factory ceiling.
    state.overrides={rawOverrides[0],rawOverrides[1],rawOverrides[2]};
    if(selected>=0){
        if(rawOverrides[0])state.active.sustained=rawOverrides[0];
        if(rawOverrides[1])state.active.shortBoost=rawOverrides[1];
        if(rawOverrides[2])state.active.peak=rawOverrides[2];
    }
    return state;
}
}
