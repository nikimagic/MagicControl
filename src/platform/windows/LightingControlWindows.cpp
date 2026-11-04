#include "LightingControlWindows.h"
#include "MagicControl/LaptopLightingResource.h"
#include "SettingsSnapshotWindows.h"
#include "MagicControl/KeyboardPulsePolicy.h"
#include "IteLightingWindows.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/DeviceOperation.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace mc::gui {
bool observeExternalLightingLevel(ExternalLightingLevelContext& c,int level){
    // Uniwill events must never seed another OEM's state or preferences.
    if(c.preview||c.oem)return false;
    level=std::clamp(level,0,4);++c.revision;c.observedLevel=level;
    if(!c.operationBusy()){
        try{c.remember(level);}catch(const std::exception& error){if(c.log)c.log("CONFIG keyboard level save failed: "+std::string(error.what()));}
    }
    if(!c.state.valid||(c.busy&&!c.reading))return false;
    const auto previous=c.state.keyboard;
    c.state.keyboard=static_cast<std::uint8_t>((level<<5)|(level==0?2:0));
    return c.state.keyboard!=previous;
}
std::optional<HidLightingMenuChoice> selectHidLightingMenu(LightingConfiguration config,int selection){
    std::string description;
    switch(selection){
    case 1:config.effect=1;description="Моно";break;
    case 2:config.effect=2;description="Пульсация";break;
    case 3:config.effect=3;description="Волна";break;
    case 5:config.effect=5;description="Мульти";break;
    case 110:config.direction=0;description="Влево";break;
    case 111:config.direction=1;description="Вправо";break;
    default:
        if(selection<100||selection>104)return {};
        config.speed=selection-100;description="Скорость "+std::to_string(selection-99);
    }
    return HidLightingMenuChoice{config,std::move(description)};
}
int observedLightingEffect(const SettingsSnapshot& s,bool hid,LightingZone zone) noexcept {
    if(hid){const auto info=s.lighting.find(zone);if(!info||!info->configured)return -1;
        const auto effect=info->configured->effect;return effect==1?0:effect==2?1:effect==5?2:-1;}
    if(s.cycle.active)return 3;
    return (s.effect&ec::kKeyboardRainbowBit)?2:s.breathing?1:0;
}
int observedLightingBrightnessIndex(const SettingsSnapshot& s) noexcept {
    return (s.keyboard&2)!=0?0:std::clamp<int>((s.keyboard>>5)&0x07,0,4);
}
std::optional<int> observedLightingBrightness(const SettingsSnapshot& s,bool preview,bool hid,bool oem,LightingZone zone) noexcept {
    if(preview)return {};
    if(hid){const auto info=s.lighting.find(zone);return info&&info->configured?std::optional<int>(info->configured->level):std::nullopt;}
    if(oem)return s.oem.keyboardLevel;
    return s.valid&&s.keyboardSupported?std::optional<int>(observedLightingBrightnessIndex(s)):std::nullopt;
}
int observedLightingPalette(const SettingsSnapshot& s,bool hid,LightingZone zone,SingleZoneModel model,
                            std::string_view profile,const std::array<KeyboardColor,8>& previews) {
    if(hid){const auto info=s.lighting.find(zone);if(!info||!info->configured||(info->configured->effect!=1&&info->configured->effect!=2))return -1;
        for(int i=0;i<8;++i)if(info->configured->rgb==previews[i])return i;return -1;}
    const auto effect=observedLightingEffect(s,false,zone);
    if(effect==3)return static_cast<int>(s.cycle.index);
    if(effect==2)return -1;
    if(effect==1){const auto& presets=keyboardPulsePalette(model,profile).presets;
        for(int i=0;i<static_cast<int>(presets.size());++i)if(presets[i]==s.breathing)return i;return -1;}
    const auto& colors=keyboardColorPalette(model);
    for(int i=0;i<static_cast<int>(colors.size());++i)if(s.color==colors[static_cast<std::size_t>(i)])return i;
    return -1;
}
LightingButtonAvailability lightingButtonAvailability(const LightingButtonInput& input) noexcept {
    LightingButtonAvailability result;
    const bool light=input.ready&&input.supported&&input.writable;
    result.effect=light;result.native=light;
    result.custom=light&&input.effect!=1&&(input.hidPalette||input.effect==0);
    result.cycle=light&&input.effect==3;
    for(std::size_t i=0;i<result.colors.size();++i)
        result.colors[i]=light&&(input.hidPalette||(input.effect!=2&&(input.effect!=1||input.pulsePresets[i]!=0)));
    for(std::size_t i=0;i<result.levels.size();++i)
        result.levels[i]=light&&static_cast<int>(i)<=input.brightnessMaximum;
    return result;
}
KeyboardBacklightState readLighting(const LightingControlContext& x){if(x.gm)return x.gm->readKeyboard();if(!x.ec)throw std::runtime_error("RGB-контроллер недоступен");return readKeyboardBacklight(*x.ec);}
LightingZonesSnapshot readLightingZones(windows::IteLightingController& controller){return controller.read();}
namespace {
KeyboardBacklightState applyLighting(const LightingControlContext&,const KeyboardBacklightChange&);
void applyLightingCycle(const LightingControlContext&,const KeyboardCycleSettings&);
constexpr std::array<KeyboardColor,8> previews{{{255,255,255},{255,0,0},{255,80,0},{255,204,0},{0,255,0},{0,255,255},{0,0,255},{180,0,255}}};
void colorLevels(const LightingControlContext& x,const KeyboardColor& rgb){if(x.cycle)x.cycle->stop();KeyboardBacklightChange c;c.color=rgb;applyLighting(x,c);}
KeyboardBacklightState applyLighting(const LightingControlContext& x,const KeyboardBacklightChange& c){
    if(x.gm)return x.gm->setKeyboard(c);
    if(!x.ec)throw std::runtime_error("RGB-контроллер недоступен");
    return applyKeyboardBacklight(*x.ec,c);
}

void applyLightingZone(const LightingControlContext& x,const LightingConfiguration& c){if(!x.ite)throw std::runtime_error("Зона подсветки недоступна");x.ite->apply(x.zone,c);}
void applyLightingLevel(const LightingControlContext& x,int level){
    if(x.ite){auto c=x.configuration;c.level=level;applyLightingZone(x,c);return;}
    if(level==0&&x.cycle)x.cycle->stop();
    KeyboardBacklightChange c;c.level=level;
    const auto before=level>0?std::optional<KeyboardBacklightState>(readLighting(x)):std::nullopt;
    if(before&&(before->keyboard&2)){
        DevicePreferences saved;std::string error;c.expected=*before;
        if(!loadDevicePreferences(x.preferences,saved,error)&&!error.empty())throw std::runtime_error(error);
        const int effect=saved[DeviceSetting::KeyboardEffect].value_or(0);c.effect=effect==1||effect==2?effect:0;
        if(c.effect==1)c.pulse=std::clamp(saved[DeviceSetting::PulseColor].value_or(0)+1,1,4);
        if(c.effect==0&&saved[DeviceSetting::Red]&&saved[DeviceSetting::Green]&&saved[DeviceSetting::Blue])c.color=KeyboardColor{static_cast<std::uint8_t>(*saved[DeviceSetting::Red]),static_cast<std::uint8_t>(*saved[DeviceSetting::Green]),static_cast<std::uint8_t>(*saved[DeviceSetting::Blue])};
        else if(c.effect==0)c.factoryColor=true;
    }
    applyLighting(x,c);
}
void applyLightingEffect(const LightingControlContext& x,int effect){
    if(x.ite){auto c=x.configuration;c.effect=effect==0?1:effect==1?2:5;applyLightingZone(x,c);return;}
    if(!x.cycle)throw std::runtime_error("RGB-контроллер недоступен");
    const bool wasCycling=x.cycle->status().active;x.cycle->stop();
    if(effect==3){
        if(x.nativeCycleSettings){std::string error;if(!saveKeyboardCycleSettings(x.cycleSettings,error))throw std::runtime_error(error);}
        const auto before=readLighting(x);
        if(!wasCycling&&!before.breathing&&!(before.effect&0x80)){
            DevicePreferences color;for(int i=0;i<3;++i)color.values[static_cast<std::size_t>(DeviceSetting::Red)+i]=before.color[i];
            std::string error;if(!rememberDevicePreferences(color,error,x.preferences))throw std::runtime_error(error);
        }
        applyLightingCycle(x,x.cycleSettings);return;
    }
    const auto current=readLighting(x);DevicePreferences saved;std::string error;
    if(!loadDevicePreferences(x.preferences,saved,error)&&!error.empty())throw std::runtime_error(error);
    KeyboardBacklightChange c;c.effect=effect;
    if(effect==0&&saved[DeviceSetting::Red]&&saved[DeviceSetting::Green]&&saved[DeviceSetting::Blue])c.color=KeyboardColor{static_cast<std::uint8_t>(*saved[DeviceSetting::Red]),static_cast<std::uint8_t>(*saved[DeviceSetting::Green]),static_cast<std::uint8_t>(*saved[DeviceSetting::Blue])};
    if(effect==1&&!current.breathing&&saved[DeviceSetting::PulseColor])c.pulse=*saved[DeviceSetting::PulseColor]+1;
    if(effect==2&&(!(current.effect&0x80)||!saved[DeviceSetting::Red])){DevicePreferences before;for(int i=0;i<3;++i)before.values[static_cast<std::size_t>(DeviceSetting::Red)+i]=current.color[i];if(!rememberDevicePreferences(before,error,x.preferences))throw std::runtime_error(error);}
    applyLighting(x,c);
}
void applyLightingColor(const LightingControlContext& x,int index){
    if(index<0||index>=8)throw std::out_of_range("Цвет подсветки недоступен");
    if(x.ite){auto c=x.configuration;c.rgb=previews[index];if(c.effect!=2)c.effect=1;applyLightingZone(x,c);return;}
    const auto current=readLighting(x);
    if(current.breathing&&!(current.effect&0x80)){const auto preset=keyboardPulsePalette(x.model,x.profile).presets[index];if(!preset)throw std::runtime_error("Пресет не поддерживается пульсацией");KeyboardBacklightChange c;c.pulse=preset;applyLighting(x,c);return;}
    colorLevels(x,keyboardColorPalette(x.model)[index]);
}
void applyLightingCustomColor(const LightingControlContext& x,const KeyboardColor& color){if(x.ite){auto c=x.configuration;c.rgb=color;if(c.effect!=2)c.effect=1;applyLightingZone(x,c);return;}colorLevels(x,keyboardColorLevels(x.model,color));}
void applyLightingNative(const LightingControlContext& x){if(x.ite){applyLightingZone(x,LightingConfiguration{});return;}if(x.cycle)x.cycle->stop();KeyboardBacklightChange c;c.level=4;c.effect=0;if(x.gm)c.factoryColor=true;else c.color=keyboardColorPalette(x.model)[5];applyLighting(x,c);}
void applyLightingCycle(const LightingControlContext& x,const KeyboardCycleSettings& settings){if(!x.cycle)throw std::runtime_error("Цикл подсветки недоступен");x.cycle->start(settings);}
} // private lighting policy; no alternate public apply entries
LightingWriteResult executeLightingResource(const LightingControlContext& x,const LightingWriteRequest& r){
    const char* origin=r.origin==LightingWriteOrigin::Explicit?"user":r.origin==LightingWriteOrigin::Restore?"restore":r.origin==LightingWriteOrigin::Temporary?"temporary":"cycle";
    audit::Scope trace("lighting.execute","\"origin\":"+audit::quote(origin)+",\"kind\":"+std::to_string(static_cast<int>(r.kind))+",\"zone\":"+audit::quote(lightingZoneKey(x.zone)));
    if(x.laptop){
        auto result=mc::executeLightingResource(*x.laptop,r);
        if(!result.success)throw std::runtime_error(result.message);
        trace.finish("native_backend_completed");
        return result;
    }
    using K=LightingWrite;
    LightingWriteResult result;
    switch(r.kind){
    case K::Level:applyLightingLevel(x,r.value);break;
    case K::Timeout:case K::NativeUpdate:throw std::runtime_error("Таймаут подсветки недоступен");
    case K::Effect:
        if(r.value<0||r.value>3)throw std::invalid_argument("Неизвестный эффект подсветки");
        applyLightingEffect(x,r.value);break;
    case K::Palette:applyLightingColor(x,r.value);break;
    case K::CustomColor:applyLightingCustomColor(x,r.color);break;
    case K::Native:applyLightingNative(x);break;
    case K::Cycle:applyLightingCycle(x,r.cycle);break;
    case K::Zone:applyLightingZone(x,r.configuration);break;
    case K::Update:result.keyboard=applyLighting(x,r.change);break;
    case K::Pulse:{KeyboardBacklightChange change;change.pulse=r.value;result.keyboard=applyLighting(x,change);break;}
    default:throw std::invalid_argument("Unknown lighting operation");
    }
    trace.finish("backend_completed");return result;
}
LightingCycleEditResult editLightingCycleInterval(KeyboardCycleSettings& settings,int seconds,std::string& error,const LightingCycleSaver& save){
    if(seconds<1||seconds>3600)return LightingCycleEditResult::Invalid;
    if(seconds==settings.intervalSeconds)return LightingCycleEditResult::Unchanged;
    settings.intervalSeconds=seconds;
    return save(settings,error)?LightingCycleEditResult::Saved:LightingCycleEditResult::Failed;
}
LightingCycleEditResult editLightingCycleColor(KeyboardCycleSettings& settings,int index,const KeyboardColor& color,std::string& error,const LightingCycleSaver& save){
    if(index<0||index>=static_cast<int>(settings.colors.size()))return LightingCycleEditResult::Invalid;
    settings.colors[static_cast<std::size_t>(index)]=color;
    return save(settings,error)?LightingCycleEditResult::Saved:LightingCycleEditResult::Failed;
}
bool shouldRestoreLightingCycle(bool passive,bool pending,bool nativePower,bool hasCycle,
        bool keyboardConfirmed,bool keyboardSuspended,bool keyboardRestorePending){
    return passive&&pending&&nativePower&&hasCycle&&keyboardConfirmed&&!keyboardSuspended&&!keyboardRestorePending;
}
std::shared_ptr<DeviceOperation> reserveLightingCycleRestore(bool& pending){
    auto reservation=deviceOperations().tryBegin();
    if(reservation)pending=false;
    return reservation;
}
bool loadLightingCycleRestore(const std::filesystem::path& path,KeyboardCycleSettings& settings){
    DevicePreferences saved;std::string error;
    if(!loadDevicePreferences(path,saved,error)&&!error.empty())throw std::runtime_error(error);
    if(saved[DeviceSetting::KeyboardCycleEnabled]!=1)return false;
    if(!loadKeyboardCycleSettings(settings,error))throw std::runtime_error(error.empty()?"Нет сохранённых цветов цикла":error);
    return true;
}
void rememberLightingChoice(const LightingControlContext& x,LightingChoice choice,int value,const LightingObservation& state){
    if(x.ite||x.laptop)return;
    if(!state.supported)throw std::runtime_error("Состояние подсветки не подтверждено; настройки не сохранены");
    using S=DeviceSetting;DevicePreferences patch;
    if(choice==LightingChoice::Level)patch[S::KeyboardLevel]=value;
    else {
        const int effect=(state.effect&ec::kKeyboardRainbowBit)?2:state.breathing?1:0;
        patch[S::KeyboardEffect]=effect;
        if(effect==1)patch[S::PulseColor]=state.breathing-1;
        if(effect==0&&!state.cycleActive){patch[S::Red]=state.color[0];patch[S::Green]=state.color[1];patch[S::Blue]=state.color[2];}
        if(choice==LightingChoice::Native)patch[S::KeyboardLevel]=(state.keyboard&2)?0:(state.keyboard>>5);
    }
    if(x.nativeCycleSettings&&choice!=LightingChoice::Level)patch[S::KeyboardCycleEnabled]=state.currentCycleActive?1:0;
    if(choice==LightingChoice::Level&&value==0)patch[S::KeyboardCycleEnabled]=0;
    std::string error;if(!rememberDevicePreferences(patch,error,x.preferences))throw std::runtime_error("Настройка EC применена, но не сохранена: "+error);
}
void rememberObservedLightingLevel(const LightingControlContext& x,int level){
    if(x.laptop)return;DevicePreferences patch;patch[DeviceSetting::KeyboardLevel]=level;std::string error;
    if(!rememberDevicePreferences(patch,error,x.preferences))throw std::runtime_error(error);
}
}
