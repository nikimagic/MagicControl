#include "SettingsPowerSelectionWindows.h"
#include "CustomModeWindows.h"
#include "SettingsPersistenceWindows.h"
#include "../../protocols/uniwill/x5sp4nag/PowerState.h"
#include <cmath>
namespace mc::windows {
PowerButtonAvailability gmPowerButtonAvailability(const GmPowerButtons& input,const std::function<bool()>& onBattery){
    PowerButtonAvailability out;const bool ready=input.idle&&input.connected&&input.writable;
    out.modes[0]=ready&&(!input.doubleEncoding||input.codecSupported)&&!onBattery();
    out.modes[4]=ready;
    for(int i=0;i<3;++i)out.modes[i+1]=input.idle&&input.presetWritable&&input.presetLimits[i];
    out.custom=ready&&(!input.doubleEncoding||input.codecSupported)&&!onBattery();
    out.presets=out.custom;out.cancelEditing=!out.custom&&input.editing;
    out.editors=out.custom&&input.editing;return out;
}
PowerButtonAvailability honorPowerButtonAvailability(const HonorPowerButtons& input,const std::function<bool()>& onBattery){
    PowerButtonAvailability out;const bool smu=input.idle&&!input.stale&&input.tableVersion>=0x370000&&input.tableVersion<=0x370005;
    out.modes[0]=smu;
    for(int i=1;i<3;++i)out.modes[i]=input.idle&&input.presetWritable&&!onBattery();
    out.custom=out.presets=smu;out.editors=smu&&input.editing;return out;
}
PowerButtonAvailability ecPowerButtonAvailability(const EcPowerButtons& input) noexcept{
    PowerButtonAvailability out;
    for(int i=0;i<4;++i)out.modes[i]=input.enabled&&(i>0||!input.fanDefaultLocked)&&!input.needsRestore;
    if(input.wujie)out.modes[4]=input.enabled&&!input.needsRestore;
    out.custom=input.enabled&&!input.fanDefaultLocked;out.presets=input.enabled&&input.hasController;
    out.editors=out.custom&&input.editing;return out;
}
PowerButtonAvailability nativePowerButtonAvailability(const NativePowerButtons& input) noexcept{
    PowerButtonAvailability out;
    for(int i=0;i<5;++i)out.modes[i]=input.idle&&input.connected&&input.stockWritable&&i>=1&&i<=input.stockCount;
    out.custom=out.presets=input.idle&&input.connected&&input.customWritable;
    out.editors=out.custom&&input.editing;out.cancelEditing=!out.custom&&input.editing;return out;
}
PowerStartupState decodePowerStartup(PowerStartupState state,const DevicePreferences& saved,bool loaded,
    bool wujie,const std::optional<CustomPreset>& honorPreset,const std::function<std::array<int,3>(int)>& honorRecipe){
    if(loaded&&saved[DeviceSetting::ManualStapm]&&saved[DeviceSetting::ManualSlow]&&saved[DeviceSetting::ManualFast]){
        state.values={*saved[DeviceSetting::ManualStapm],*saved[DeviceSetting::ManualSlow],*saved[DeviceSetting::ManualFast]};
        state.editorsInitialized=true;
    }
    if(honorPreset)state.values=honorPreset->powerRecipe?honorRecipe(honorPreset->powerRecipe):honorPreset->power;
    if(saved[DeviceSetting::ApuMode])state.chosenMode=*saved[DeviceSetting::ApuMode];
    else if(saved[DeviceSetting::ManualPowerEnabled]==1)state.chosenMode=4;
    if(wujie){
        state.chosenMode=x5sp4nag::savedPowerButton(saved[DeviceSetting::ApuMode],saved[DeviceSetting::ManualPowerEnabled]==1,saved[DeviceSetting::ApuAutoPower]==1);
        if(saved[DeviceSetting::ApuAutoPower]==1)state.savedCustomIntent=false;
    }
    return state;
}
PowerStartupState loadPowerStartup(std::string_view profile,bool allowPreferences,bool honor,bool wujie,PowerStartupState initial){
    DevicePreferences saved;std::string error;const bool loaded=loadPowerStartupSettings(allowPreferences,saved,error);
    std::optional<CustomPreset> preset;
    if(honor&&initial.savedCustomIntent){const auto library=loadCustomPresetLibrary(profile);preset=library.slots.at(library.selected);}
    return decodePowerStartup(initial,saved,loaded,wujie,preset,honorCustomRecipe);
}
int selectedPowerMode(const PowerSelectionContext& c) {
    if(c.oemControl&&c.nativeCustom){
        if(c.powerEditing)return c.customIndex;
        if(c.state.oem.customCpuPowerActive==true)return c.customIndex;
        return c.state.oem.connected&&c.state.oem.stockCpuProfile?*c.state.oem.stockCpuProfile+1:-1;
    }
    // A stock-only typed backend must not inherit Honor's Custom recipes.
    if(c.oemControl&&!c.gm6Controller&&!c.oemPolicySupported)
        return c.state.oem.connected&&c.state.oem.stockCpuProfile?*c.state.oem.stockCpuProfile+1:-1;
    const auto& state=c.state;const auto& powerValues=c.powerValues;
    const auto chosenPowerMode=c.chosenPowerMode;const auto savedCustomIntent=c.savedCustomIntent;
    const auto pendingPerformance=c.pendingPerformance;const auto gm6NativePower=c.gm6NativePower;
    const auto gm6Controller=c.gm6Controller;const auto oemControl=c.oemControl;const auto ec=c.ec;const auto powerEditing=c.powerEditing;

        if(!gm6NativePower&&(chosenPowerMode==c.customIndex||savedCustomIntent)&&!pendingPerformance){
            const auto match=[&](const std::array<std::optional<double>,3>& actual){for(int i=0;i<3;++i)if(!actual[i]||std::abs(*actual[i]-powerValues[i])>=0.6)return false;return true;};
            if(gm6Controller&&state.gm6.connected&&state.gm6.custom&&state.gm6.ramActive&&state.gm6.limits==powerValues)return c.customIndex;
            if(oemControl&&!state.apu.stale&&match({state.apu.stapmLimitWatts,state.apu.slowLimitWatts,state.apu.fastLimitWatts}))return c.customIndex;
            const auto& p=state.power.overrides;
            if(ec&&p.sustained==powerValues[0]&&p.shortBoost==powerValues[1]&&p.peak==powerValues[2])return c.customIndex;
        }
        if(gm6Controller){if(!state.gm6.connected)return -1;
            if(gm6NativePower){
                if(state.gm6.autoPowerActive)return 4;
                if(state.gm6.economyActive)return 0;
                if(state.gm6.stockCpuActive)return state.gm6.cpuPreset+1;
                if(state.gm6.limits!=std::array<int,3>{})return c.customIndex;
                if(!state.gm6.nativeProfile)return -1;
                if(*state.gm6.nativeProfile==gm6::NativeProfile::Custom)return c.customIndex;
                return static_cast<int>(*state.gm6.nativeProfile)+1;
            }
            if(state.gm6.limits==std::array<int,3>{})return 4;if(state.gm6.limits==std::array<int,3>{10,15,20})return 0;if(state.gm6.cpuPreset>=0)return state.gm6.cpuPreset+1;return 5;}
        if(oemControl){
            if(powerEditing)return 4;
            if(chosenPowerMode!=0&&chosenPowerMode!=4&&state.oem.stockCpuProfile)return *state.oem.stockCpuProfile+1;
            if(state.apu.stale||!state.apu.stapmLimitWatts||!state.apu.slowLimitWatts||!state.apu.fastLimitWatts)return -1;
            const std::array<double,3> values{*state.apu.stapmLimitWatts,*state.apu.slowLimitWatts,*state.apu.fastLimitWatts};
            for(int preset=0;preset<1;++preset){const auto expected=honorCustomRecipe(preset);bool match=true;
                for(int i=0;i<3;++i)match=match&&std::abs(values[i]-expected[i])<0.6;
                if(match)return chosenPowerMode==4?4:preset;}
            return chosenPowerMode==4?4:state.oem.stockCpuProfile?*state.oem.stockCpuProfile+1:-1;
        }
        if(c.wujie)return x5sp4nag::powerButton(state.power,chosenPowerMode==4,chosenPowerMode==c.customIndex);
        const auto& v = state.power.overrides;
        if (!(v.sustained && v.shortBoost && v.peak)) return -1;
        if(*v.sustained==0&&*v.shortBoost==0&&*v.peak==0){
            const auto mode=state.power.rawMode&0x90;
            return mode==0x80?1:mode==0?2:mode==0x10?3:-1;
        }
        if (chosenPowerMode == 4) return 4; // Manual may intentionally equal a preset.
        const std::array<int, 3> actual{*v.sustained, *v.shortBoost, *v.peak};
        for (int i = 0; i < 4; ++i) if (actual == kApuPowerPresets[i] || actual == apuPresetLimits(i, true)) return i;
        return 4;
    
}
}
