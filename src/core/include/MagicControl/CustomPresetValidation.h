#pragma once
#include "MagicControl/CustomPresets.h"
#include "MagicControl/DeviceProfile.h"
#include <stdexcept>
namespace mc {
// File-format validation only. Runtime backend admission remains independent.
inline void validateCustomPresetForProfile(const DeviceProfile& device,const CustomPreset& p){
    const auto& policy=device.customPreset;if(p.powerRecipe<0||p.powerRecipe>3)throw std::runtime_error("Custom presets: invalid power recipe");
    if(!p.powerRecipe){
        if(p.power!=std::array<int,3>{})for(int i=0;i<3;++i)if(p.power[i]<policy.minimum||p.power[i]>policy.maximum[i])throw std::runtime_error("Custom presets: power outside model range");
        for(int i=0;i<2;++i)if(policy.orderedPairs[i]&&p.power[i]>p.power[i+1])throw std::runtime_error("Custom presets: PL order");
    }
    if(p.powerOptions.size()>32)throw std::runtime_error("Custom presets: too many power options");
    for(const auto& [key,value]:p.powerOptions)if(key.empty()||key.size()>64||key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")!=std::string::npos||value<0||value>100000)throw std::runtime_error("Custom presets: invalid optional power setting");
    for(int ch=0;ch<2;++ch)for(int i=0;i<(p.curvesReady?device.fanPointCounts[ch]:16);++i){const auto& c=p.curves[ch];const auto& v=c[i];
        if(v.up<0||v.up>255||v.down<0||v.down>255||v.duty<0||v.duty>100||
           (i&&(v.up<c[i-1].up||v.down<c[i-1].down||v.duty<c[i-1].duty)))throw std::runtime_error("Custom presets: invalid curve");}
}
}
