#include "MagicControl/DeviceProfile.h"
#include "MagicControl/SettingsPreview.h"
#include <iostream>
#include <stdexcept>
#include <type_traits>

// State is plain ephemeral data: it contains no backend, persistence callback,
// live measurement, or guessed WMI range. UI adapters must separately prove
// preview admission before accessing it.
static_assert(std::is_trivially_copyable_v<mc::SettingsPreviewState>);
static_assert(std::is_standard_layout_v<mc::SettingsPreviewState>);
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
}
int main(){try{
    const auto profiles=mc::deviceProfiles();
    check(!profiles.empty(),"empty profile registry");
    for(const auto* profile:profiles){
        check(profile!=nullptr,"null profile");
        mc::SettingsPreviewState state,other;
        check(state.power==-1&&state.preset==-1&&state.batteryMode==-1&&state.brightness==-1,
              "preview must not claim observed state");
        state.stockPower(1);check(state.power==1&&!state.customEditing,"stock selection");
        state.customPower(4);check(state.customEditing&&state.power==4&&state.preset==0,"Custom default preset");
        for(int slot=0;slot<3;++slot){state.customPreset(slot,4);check(state.preset==slot&&state.customEditing,"Custom slot");}
        state.customPreset(-1,4);state.customPreset(3,4);check(state.preset==2,"invalid preset accepted");
        state.stockPower(2);check(state.power==2&&!state.customEditing&&state.preset==2,"stock switch lost draft");
        state.effect=2;state.color=5;state.brightness=1;state.batteryUpper=2;state.batteryMode=1;state.batteryAging=0;
        check(state.effect==2&&state.color==5&&state.brightness==1&&state.batteryUpper==2&&state.batteryMode==1&&state.batteryAging==0,"independent preview choices");
        check(other.power==-1&&other.preset==-1&&other.batteryUpper==-1&&other.effect==-1,"preview state leaked to another page");
        mc::SettingsPreviewState reopened;
        check(reopened.power==-1&&reopened.preset==-1&&reopened.batteryMode==-1,"preview unexpectedly persisted");
    }
    std::cout<<"settings preview: "<<profiles.size()<<" profiles, "<<checks<<" checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
