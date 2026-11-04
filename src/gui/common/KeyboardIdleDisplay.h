#pragma once
#include "Geometry.h"
#include "Localization.h"
#include "MagicControl/KeyboardBacklight.h"
#include <chrono>
namespace mc::gui::ui {
inline std::string keyboardIdleValue(const std::optional<KeyboardIdleState>& state){
    if(!state)return "—";
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(now<state->sampledAtMs||now-state->sampledAtMs>2500||state->powerState!=1)return "—";
    if((state->flags&1)||state->hostPowerDisabled)return translate("Отключено");
    // Expired is not a claim of physical darkness: AC/DC restore can relight
    // the keyboard while the EC idle bit is still set.
    if(state->flags&4)return "0";
    if(!state->workingLevel)return "—";
    return std::to_string(std::max(0,int(state->limit)-int(state->elapsed)));
}
struct KeyboardIdleGeometry {Rect caption,value,unit;};
inline KeyboardIdleGeometry placeKeyboardIdle(Rect& heading,Scale s,int captionWidth,int valueWidth,int unitWidth){
    const auto right=heading.right;const int gap=s(8),field=std::max(s(52),valueWidth+s(16));
    const int left=std::max(heading.left+s(100),right-(captionWidth+field+unitWidth+gap*2));
    const auto top=heading.top,bottom=heading.bottom;
    KeyboardIdleGeometry g{{left,top,right-field-unitWidth-gap*2,bottom},
        {right-field-unitWidth-gap,top,right-unitWidth-gap,bottom},{right-unitWidth,top,right,bottom}};
    heading.right=std::max(heading.left,left-gap);return g;
}
}
