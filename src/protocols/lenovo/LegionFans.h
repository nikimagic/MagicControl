#pragma once
#include "MagicControl/NativeFanCurve.h"
#include "MagicControl/FanControlLimits.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
namespace mc::lenovo {
// Exact NDS32 KWCN54WW:13records *16byte SRAM rows. ROM rows have an extra
// stage byte; E63C strips it when copying to81F00. Do NOT write ROM/PWM/GPIO.
inline constexpr std::uint16_t tableAddress=0xdf00,tableFlags=0xdfe0,tableCount=0xdfef;
inline constexpr int userPoints=10,nativeCapacity=13,rowBytes=16;
using FanPacket=std::array<std::uint8_t,nativeCapacity*rowBytes>;
struct FanBounds {std::array<int,2> minimumRpm{},maximumRpm{};};
// 255 is the application's empty-point sentinel, never an active EC temperature.
// KWCN54WW factory tables use 127 as the final guard threshold; preserve all
// baseline inactive-sensor bytes rather than translating 255 into their slots.
inline int curvePoints(const NativeFanCurves& curves){int count{};bool empty=false;
    for(int i=0;i<userPoints;++i){const auto& p=curves[0][i];if(p!=curves[1][i])throw std::runtime_error("Lenovo: current Manual is CPU=GPU");
        if(p.temperature==255){empty=true;continue;}
        if(empty||!fanInput::validTemperature(p.temperature)||!fanInput::validPercent(p.duty)||
           (count&&(p.temperature<curves[0][count-1].temperature||p.duty<curves[0][count-1].duty)))throw std::runtime_error("Lenovo: invalid shared Manual curve");++count;}
    if(!count)throw std::runtime_error("Lenovo: Manual curve is empty");return count;
}
inline bool activeLevelMapValid(const std::array<int,10>& map){
    return std::all_of(map.begin(),map.end(),[](int value){return value>=0&&value<nativeCapacity;});
}
inline bool transitionPrefix(std::span<const std::uint8_t> before,std::span<const std::uint8_t> after,std::span<const std::uint8_t> actual){
    if(before.size()!=after.size()||before.size()!=actual.size())return false;
    bool tail=false;for(std::size_t i=0;i<before.size();++i){
        if(!tail&&actual[i]==after[i])continue;tail=true;if(actual[i]!=before[i])return false;
    }return true;
}
inline FanPacket encodeSharedCurve(const NativeFanCurves& curves,const FanPacket& baseline,int baselineCount,const FanBounds& bounds){
    if(baselineCount<1||baselineCount>nativeCapacity)throw std::runtime_error("Lenovo: invalid native FAN row count");
    for(int ch=0;ch<2;++ch)if(bounds.minimumRpm[ch]<100||bounds.maximumRpm[ch]<bounds.minimumRpm[ch]||bounds.maximumRpm[ch]>20000)throw std::runtime_error("Lenovo: WMI RPM bounds unavailable");
    // EC encodes RPM in 100-RPM steps. Reject intervals with no representable
    // step rather than round below firmware's minimum.
    for(int ch=0;ch<2;++ch)if((bounds.minimumRpm[ch]+99)/100>bounds.maximumRpm[ch]/100)throw std::runtime_error("Lenovo: WMI RPM bounds contain no encodable step");
    const int points=curvePoints(curves);
    auto packet=baseline;
    for(int i=0;i<=points;++i){
        const auto source=std::min(i,baselineCount-1)*rowBytes;const auto row=i*rowBytes;
        // Preserve ramp fields and all non-CPU/GPU protective sensor pairs.
        std::copy_n(baseline.begin()+source,rowBytes,packet.begin()+row);
        const int temperature=i<points?curves[0][i].temperature:127;
        const int down=i?std::max(0,curves[0][i-1].temperature-3):0;
        for(int ch=0;ch<2;++ch){const int requested=i<points?(bounds.maximumRpm[ch]*curves[0][i].duty+99)/100:bounds.maximumRpm[ch];
            const int rpm=std::clamp(requested,bounds.minimumRpm[ch],bounds.maximumRpm[ch]);
            packet[row+ch]=static_cast<std::uint8_t>(std::min(bounds.maximumRpm[ch]/100,(rpm+99)/100));}
        packet[row+4]=packet[row+6]=static_cast<std::uint8_t>(temperature);
        packet[row+5]=packet[row+7]=static_cast<std::uint8_t>(down);
    }
    return packet;
}
}
