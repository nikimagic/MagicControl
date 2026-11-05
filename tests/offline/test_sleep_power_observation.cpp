#include "MagicControl/DeviceProfile.h"
#include "../../src/platform/linux/SettingsObservationEpoch.h"
#ifdef _WIN32
#include "../../src/platform/windows/SettingsSnapshotWindows.h"
#endif
#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
// A deterministic memory-only provider; it never creates an OS, EC or SMU backend.
struct FakeSmu {
    std::array<std::optional<double>,3> value{};
    bool stale{};
};
}
int main(){try{
    unsigned profiles{};
    for(const auto* profile:mc::deviceProfiles()){
        check(profile&&!profile->id.empty(),"catalogue contains invalid profile");
        mc::linux_platform::SettingsObservationEpoch epoch;
        const auto before=epoch.capture();FakeSmu old{{10.,10.,20.},false};
        epoch.transition(true);check(!epoch.current(before),"pre-sleep observation survived sleep");
        const auto sleeping=epoch.capture();epoch.transition(false);
        check(!epoch.current(sleeping)&&!epoch.current(before),"pre-wake observation survived resume");
        FakeSmu fresh{{15.,25.,45.},false};const auto now=epoch.capture();
        check(epoch.current(now)&&fresh.value!=old.value,"post-wake observation was not independent");
        FakeSmu unavailable{{},true};check(!unavailable.value[0]&&unavailable.stale,"missing SMU became a confirmed value");
        ++profiles;
    }
    check(profiles>0,"no device profiles exercised");
#ifdef _WIN32
    using namespace mc::gui;
    SettingsSnapshot sample;sample.valid=true;sample.power.overrides={15,25,45};
    sample.power.acceptedValid=true;sample.power.accepted={10,10,20};
    FakeSmu smu{{12.,18.,25.},false};
    const auto collect=[&]{sample.apu.stapmLimitWatts=smu.value[0];sample.apu.slowLimitWatts=smu.value[1];
        sample.apu.fastLimitWatts=smu.value[2];sample.apu.stale=smu.stale;};
    collect();
    check(observedSettingsPowerLimits(sample,false,true)==std::array<std::optional<double>,3>{10.,10.,20.},"EC accepted values replaced by requested or SMU values");
    const auto audit=settingsSnapshotAudit(sample);
    check(audit.find("\"stapm\":12")!=std::string::npos&&audit.find("\"accepted_valid\":true")!=std::string::npos,"independent fake SMU observation lost");
    sample.power.acceptedValid=false;
    check(!observedSettingsPowerLimits(sample,false,true)[0],"missing accepted EC value silently borrowed SMU");
    smu.value={15.,25.,45.};collect();sample.power.acceptedValid=true;sample.power.accepted={15,25,45};
    check(settingsSnapshotAudit(sample).find("\"fast\":45")!=std::string::npos,"updated fake SMU value not observed");
    smu.value={};smu.stale=true;collect();
    check(wujiePowerComparisonText(sample).find("SMU")!=std::string::npos&&
        settingsSnapshotAudit(sample).find("\"stale\":true")!=std::string::npos,"unavailable/stale fake SMU source not represented");
#endif
    std::cout<<"PASS memory-only sleep observation epochs for "<<profiles<<" profiles; no EC/SMU hardware\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
