#pragma once
#include "QuickSwitch.h"
#include "KeysLifecycle.h"
#include "DeviceProfile.h"
#include <string_view>
namespace mc {
enum class KeysBackend { Unavailable, Preferences, System, UniwillEc, HuaweiHwmi, Firmware };
// Only a documented physical absence hides an action. Unknown capability and
// unsupported transports remain visible and never imply an absent keyboard key.
inline bool keyActionVisible(std::string_view profile,KeyAction action) noexcept {
    if(action!=KeyAction::Copilot)return true;
    for(const auto* device:deviceProfiles())
        if(device&&device->id==profile)return !profileResourceAbsent(*device,"keyboard-copilot");
    return true;
}
// Catalog selection cannot authorize a device write. System actions target the actual host.
constexpr KeysBackend keysBackend(std::string_view profile,KeyAction action,bool linuxHost,bool preview=false,bool present=true){
    if(!present)return KeysBackend::Unavailable;
    if(action==KeyAction::Osd)return KeysBackend::Preferences;
    if(action==KeyAction::Microphone)return KeysBackend::System;
    if(action==KeyAction::Copilot)return linuxHost||preview?KeysBackend::Unavailable:KeysBackend::Firmware;
    if(action>=KeyAction::Wifi&&action<=KeyAction::Touchpad)return linuxHost?KeysBackend::Unavailable:KeysBackend::System;
    if(preview)return KeysBackend::Unavailable;
    if(profile=="huawei-hlyl-wxx9"&&action==KeyAction::Fn)return KeysBackend::HuaweiHwmi;
    if(!linuxHost&&uniwillQuickSwitchProfile(profile)&&
       (action==KeyAction::Windows||action==KeyAction::Fn||action==KeyAction::Usb||action==KeyAction::Ac))return KeysBackend::UniwillEc;
    return KeysBackend::Unavailable;
}
}
