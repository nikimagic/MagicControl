#include "SettingsPersistenceWindows.h"
#include "MagicControl/BatteryEcPolicy.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/BatteryUiPolicy.h"
#include "SettingsSnapshotWindows.h"
#include "LightingControlWindows.h"
#include <format>
#include <stdexcept>

namespace mc::windows {
namespace {
void save(const std::filesystem::path& path,const DevicePreferences& patch){
    std::string error;
    if(!rememberDevicePreferences(patch,error,path))throw std::runtime_error("Настройка EC применена, но не сохранена: "+error);
}

}
bool loadPowerStartupSettings(bool allowed,DevicePreferences& saved,std::string& error){
    return allowed&&loadDevicePreferences(portableSettingsPath("device-settings.ini"),saved,error);
}
void rememberObservedPower(const std::filesystem::path& path,bool wujie,int preset,bool manual,std::uint8_t mode,const EcPowerLimits& overrides){
    DevicePreferences patch;
    if(manual){
        if(wujie)patch[DeviceSetting::ApuAutoPower]=0;
        patch[DeviceSetting::ApuMode]=4;patch[DeviceSetting::NativeCpuPresets]=0;patch[DeviceSetting::ManualPowerEnabled]=1;
        const auto value=mode&ec::kPerformancePresetMask;
        patch[DeviceSetting::Performance]=value==0x80?0:value==0x10?2:value==0?1:-1;
        patch[DeviceSetting::ManualStapm]=overrides.sustained;
        patch[DeviceSetting::ManualSlow]=overrides.shortBoost;
        patch[DeviceSetting::ManualFast]=overrides.peak;
    }else{
        if(wujie)patch[DeviceSetting::ApuAutoPower]=preset==4?1:0;
        if(preset<4)patch[DeviceSetting::ApuMode]=preset;
        patch[DeviceSetting::ManualPowerEnabled]=0;
        if(preset<4)patch[DeviceSetting::NativeCpuPresets]=preset>0?1:0;
    }
    save(path,patch);
}
}
namespace mc::gui {
void rememberSettingsChoice(const SettingsRememberContext& c){
    using C=SettingsRememberChoice;
    const bool light=c.choice>=C::LightingLevel&&c.choice<=C::LightingCycle;
    if(c.choice==C::Ignore||c.oem||(c.hidLighting&&light&&c.choice!=C::LightingCycle))return;
    if((c.nativePower||c.nativeBattery)&&!light)return;
    if(light){
        if(c.choice==C::LightingCycle){
            if(!c.actual.keyboardSupported)throw std::runtime_error("Состояние подсветки не подтверждено; настройки не сохранены");
            return;
        }
        const auto choice=c.choice==C::LightingLevel?LightingChoice::Level:
            c.choice==C::LightingEffect?LightingChoice::Effect:
            c.choice==C::LightingColor?LightingChoice::Color:
            c.choice==C::LightingCustomColor?LightingChoice::CustomColor:LightingChoice::Native;
        rememberLightingChoice(c.lightingContext(),choice,choice==LightingChoice::Level?c.index:0,
            {c.actual.keyboardSupported,c.actual.cycle.active,c.cycleActive(),c.actual.keyboard,c.actual.effect,c.actual.breathing,c.actual.color});

    }else if(c.choice==C::Performance||c.choice==C::ManualPower){
        const bool manual=c.choice==C::ManualPower;
        windows::rememberObservedPower(c.preferences(),c.wujiePower(),manual?4:c.index,manual,c.actual.performance,c.actual.power.overrides);
    }else return;
    if(c.log)c.log(std::format("CONFIG saved: SETs command={}",c.command));
}
}
