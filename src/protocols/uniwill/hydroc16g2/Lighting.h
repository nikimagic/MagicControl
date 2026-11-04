#pragma once
#include "KeyboardHid.h"
#include "MagicControl/LightingZones.h"
#include <algorithm>
namespace mc::hydroc16g2 {
// Recovered LM_ITE_RGB:1217,1248,1325,1395,3245; HIDLightbar4:209,
// HIDLightbar4_logo:151, HIDLightbar4_hinge. Save=0: runtime, no flash/UEFI.
using LightingPacket=std::array<std::uint8_t,9>;
constexpr std::uint8_t lightingControl(LightingZone zone){
    switch(zone){case LightingZone::Lightbar:return 34;case LightingZone::Logo:return 35;
    case LightingZone::Hinge:return 33;default:return 2;}
}
constexpr bool validLightingConfiguration(const LightingConfiguration& c){
    return c.level>=0&&c.level<=4&&c.speed>=0&&c.speed<=4&&c.direction>=0&&c.direction<=1&&
        (c.effect==1||c.effect==2||c.effect==3||c.effect==5);
}
constexpr LightingPacket lightingEffect(LightingZone zone,const LightingConfiguration& c){
    constexpr std::array<std::uint8_t,5> speed{10,7,5,3,1};
    constexpr std::array<std::uint8_t,5> keyboardLight{0,8,22,36,50};
    const auto light=zone==LightingZone::Keyboard?keyboardLight[c.level]:static_cast<std::uint8_t>(25*c.level);
    return {0,8,lightingControl(zone),static_cast<std::uint8_t>(c.effect),speed[c.speed],light,1,
        static_cast<std::uint8_t>(c.direction==0?2:1),0}; // OEM Translate_ITE_DirectionValue: RightLeft2, LeftRight1.
}
constexpr LightingPacket lightingColor(LightingZone zone,const LightingConfiguration& c){
    const auto control=zone==LightingZone::Keyboard?0:lightingControl(zone)-32;
    return {0,20,static_cast<std::uint8_t>(control),1,c.rgb[0],c.rgb[1],c.rgb[2],0,0};
}
}
