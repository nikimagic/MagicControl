#pragma once
#include "KeyboardBacklight.h"
#include <array>
#include <string_view>
namespace mc {
// Keep swatch order; only hardware preset values are model specific.
struct KeyboardPulsePalette {std::array<std::uint8_t,8> presets;bool numbered;};
inline constexpr KeyboardPulsePalette wujiePulsePalette{{1,2,0,0,3,0,4,0},false};
inline constexpr KeyboardPulsePalette gm6ObservedPulsePalette{{1,2,0,0,3,0,4,0},false};
inline constexpr KeyboardPulsePalette gmPulsePalette{{4,1,0,0,2,0,3,0},false};
inline constexpr KeyboardPulsePalette unknownGmPulsePalette{{1,2,3,4,0,0,0,0},true};
inline constexpr const KeyboardPulsePalette& keyboardPulsePalette(SingleZoneModel model,std::string_view profile={}){
    if(model!=SingleZoneModel::Gm6)return wujiePulsePalette;
    if(profile=="mechrevo-gm6ir0c")return gm6ObservedPulsePalette;
    return profile=="mechrevo-gm5hg0a"?gmPulsePalette:unknownGmPulsePalette;
}
}
