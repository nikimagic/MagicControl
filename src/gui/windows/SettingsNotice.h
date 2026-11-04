#pragma once
#include "SettingsControlIds.h"
#include "../common/OsdMessages.h"
#include "../common/CpuPowerLabels.h"
#include "../common/KeyboardPulsePalette.h"

namespace mc::gui {
// Presentation context only. This cannot select or authorize a device action.
struct SettingsNoticeContext {
    int id{};std::string_view profile;bool honor{},gm{},onBattery{},batteryAuto{},ite{};
    LightingZone zone{LightingZone::Keyboard};int brightnessMaximum{},effect{};
    KeyboardPulsePalette pulse{};BatteryModeLabels batteryModeLabels{};
};
inline osd::Message settingsNotice(const SettingsNoticeContext& c){
    using namespace settings_controls;const int id=c.id;
    const auto lighting=[&](std::string action,std::optional<int> level={}){
        return osd::lighting(c.ite?c.zone:LightingZone::Keyboard,std::move(action),level,c.brightnessMaximum);};
    if(id>=kBatteryVoltageFirst&&id<kBatteryVoltageFirst+3)
        return {"battery",std::string("Батарея: ")+ui::batteryModeNames(c.batteryModeLabels)[id-kBatteryVoltageFirst],"Применяю…",osd::Icon::Battery};
    if(id==kUsbPriorityBattery||id==kUsbPriorityPerformance)
        return {"power",id==kUsbPriorityBattery?"USB-C · Battery":"USB-C · Performance","Применяю…",osd::Icon::Power};
    if(id==kApuPolicyOff||id==kApuPolicyOn||id==kApuPolicyOriginal){
        if(c.honor)return {"power","Turbo Huawei",id==kApuPolicyOn?"Включаю…":"Выключаю…",osd::Icon::Power};
        return {"ec-policy",std::string("EC · ")+ui::ecPolicyButtonNames[id-kApuPolicyOff],"Применяю…",osd::Icon::Power};
    }
    if(customPresetCommand(id))return {"performance","Custom","Применяю пресет Custom…",osd::Icon::Power};
    if(id==kManualPower)return {"performance",c.gm?"Intel CPU: Custom":"APU: Custom","Применяю…",osd::Icon::Power};
    if(id==kBatteryAutomatic)return {"power",c.onBattery?"Limiter EC":"Turbo EC",c.batteryAuto?"Выключаю…":"Включаю…",osd::Icon::Power};
    if(id>=kBatteryFirst&&id<kBatteryFirst+ui::batteryThresholdCount){const int value=ui::batteryThresholdValues[id-kBatteryFirst];
        return {"battery","Лимит зарядки "+std::to_string(value)+"%","Применяю…",osd::Icon::Battery,value};}
    if(id>=kPerformanceFirst&&id<kPerformanceFirst+ui::cpuPowerButtonCount(c.profile)){
        const auto names=ui::cpuPowerLabels(c.profile);return {"power",names[id-kPerformanceFirst],"Применяю…",osd::Icon::Power};}
    if(id>=kLevelFirst&&id<kLevelFirst+5){const int level=id-kLevelFirst;
        return lighting(level?std::to_string(level)+" / "+std::to_string(c.brightnessMaximum):"Выкл.",level);}
    if(id>=kEffectFirst&&id<kEffectFirst+4){const char* modes[]{"Моно","Пульсация","Мульти","Цикл 6"};return lighting(modes[id-kEffectFirst]);}
    if(id==kNativeMode)return lighting("Default");
    if(id>=kColorFirst&&id<kColorFirst+8){const char* colors[]{"Белый","Красный","Оранжевый","Жёлтый","Зелёный","Голубой","Синий","Фиолетовый"};
        const int index=id-kColorFirst;
        if(c.effect==3)return lighting("Цикл 6 · Цвет "+std::to_string(index+1));
        if(c.effect==1&&!c.ite&&c.pulse.numbered)return lighting("Пульсация · Preset "+std::to_string(c.pulse.presets[index]));
        return lighting(std::string(c.effect==1?"Пульсация · ":"")+colors[index]);}
    if(id==kCustomColor)return lighting("Свой цвет…");
    return {"settings","Настройки","Применяю…",osd::Icon::Information};
}
}
