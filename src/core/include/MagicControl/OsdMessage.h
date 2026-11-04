#pragma once
#include <cstdint>
#include "LightingZones.h"
#include <utility>
#include <optional>
#include <string>
namespace mc::gui::osd {
enum class Icon { Keyboard, Sound, Muted, Brightness, Microphone, Touchpad,
                  Lock, Battery, Power, Camera, Wifi, Display, Information,
                  Bluetooth, Windows, Usb, Notifications, Fan };
struct Message {
    std::string key, title, detail;
    Icon icon{Icon::Information};
    std::optional<int> percent{};
    bool error{}, pending{};
    std::uint64_t receivedAt{};
    std::uint64_t issuedAt{};
};
inline Message lighting(mc::LightingZone zone,std::string action,std::optional<int> level={},int maximum=4){
    const auto key=zone==mc::LightingZone::Keyboard?std::string("keyboard"):"lighting-"+std::string(mc::lightingZoneKey(zone));
    const char* title=zone==mc::LightingZone::Keyboard?"Клавиатура":zone==mc::LightingZone::Lightbar?"Полоса":zone==mc::LightingZone::Logo?"Логотип":"Шарнир";
    std::optional<int> percent;
    if(level&&maximum>0&&*level>=0&&*level<=maximum)percent=*level*100/maximum;
    return {key,std::string(title)+(action.empty()?"":" · "+action),"Применяю…",
        zone==mc::LightingZone::Keyboard?Icon::Keyboard:Icon::Brightness,percent};
}
inline Message fanMode(std::string mode){
    return {"fans","Вентиляторы · "+mode,"Применяю…",Icon::Fan};
}

}
