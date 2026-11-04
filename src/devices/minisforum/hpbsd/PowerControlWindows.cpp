#include "PowerControlWindows.h"
#include "FanControlWindows.h"
#include "TelemetryWindows.h"
#include "../../../platform/windows/ConfigurationRestoreWindows.h"
#include "../../../protocols/minisforum/HpbsdPower.h"
#include "../../../platform/windows/ApuPowerWindows.h"
#include "../../../platform/windows/CustomModeWindows.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/Logging.h"
#include <windows.h>
#include <cmath>
#include <fstream>
#include <mutex>

namespace mc::windows {
namespace {
constexpr auto preferenceName="hpbsd-power-mode.ini";
struct PowerRuntime {
    std::mutex hardware;
    ApuPowerReader reader;
    std::shared_ptr<HpbsdFanController> fans{hpbsdFanController()};
    std::unique_ptr<ConfigurationRestore> configuration{std::make_unique<ConfigurationRestore>("minisforum-hpbsd")};
    std::atomic<bool> paused{},stopping{};
    ~PowerRuntime(){stopping=true;configuration.reset();}
};
std::mutex runtimeGate;
std::shared_ptr<PowerRuntime> activeRuntime;
bool admitted(){return !wcsstr(GetCommandLineW(),L"--no-ec")&&HpbsdTelemetryReader::supportedHost();}
std::shared_ptr<PowerRuntime> runtime(){
    if(!admitted())return {};
    std::lock_guard lock(runtimeGate);
    if(!activeRuntime)activeRuntime=std::make_shared<PowerRuntime>();
    return activeRuntime;
}
class HpbsdPowerControl final:public LaptopControl {
    std::shared_ptr<PowerRuntime> state_;
public:
    explicit HpbsdPowerControl(std::shared_ptr<PowerRuntime> state):state_(std::move(state)){}
    std::string_view customPowerPolicyId()const noexcept override{return "minisforum-hpbsd-windows-smu";}
    std::optional<CpuPowerChoice> savedCpuPowerChoice()const override{return savedHpbsdPowerChoice();}
    std::array<int,3> customCpuPowerMaximum()const override{return {hpbsd::customPowerMaximum,hpbsd::customPowerMaximum,hpbsd::customPowerMaximum};}
    std::array<int,3> customCpuPowerRecipe(int slot)const override{
        if(slot<0||slot>=3)throw std::invalid_argument("HPBSD: invalid Custom slot");return hpbsd::customPowerRecipes[slot];
    }
    LaptopControlSnapshot readTelemetry()override{
        LaptopControlSnapshot out;out.connected=admitted();out.source="HPBSD · native BIOS power profiles";return out;
    }
    LaptopControlSnapshot read()override{
        auto out=readTelemetry();if(!out.connected)return out;
        std::lock_guard lock(state_->hardware);
        const auto power=state_->reader.read(true);out.status=power.status;
        const bool fresh=!power.stale&&power.tableVersion==0x4c0009&&power.stapmLimitWatts&&power.slowLimitWatts&&power.fastLimitWatts;
        SYSTEM_POWER_STATUS supply{};
        out.stockCpuProfileWritable=fresh&&GetSystemPowerStatus(&supply)&&supply.ACLineStatus==1&&!state_->paused&&!state_->stopping;
        out.customCpuPowerWritable=out.stockCpuProfileWritable;
        if(fresh){
            try{const auto choice=savedCpuPowerChoice();out.customCpuPowerActive=choice&&choice->custom&&
                std::abs(*power.stapmLimitWatts-choice->values[0])<.05&&std::abs(*power.slowLimitWatts-choice->values[1])<.05&&std::abs(*power.fastLimitWatts-choice->values[2])<.05;}
            catch(const std::exception& error){out.customCpuPowerWritable=false;out.status+="; "+std::string(error.what());}
        }
        if(fresh){const std::array<double,3> actual{*power.stapmLimitWatts,*power.slowLimitWatts,*power.fastLimitWatts};
            for(int p=0;p<2;++p){bool same=true;for(int i=0;i<3;++i)same=same&&std::abs(actual[i]*1000-hpbsd::stockPower(p)[i])<50;if(same)out.stockCpuProfile=p;}}
        return out;
    }
    void setStockCpuProfile(int profile)override{
        static_cast<void>(hpbsd::stockPower(profile));
        std::lock_guard lock(state_->hardware);
        if(!admitted()||state_->paused||state_->stopping||!state_->fans)throw std::runtime_error("HPBSD: power controller unavailable/suspended");
        static_cast<void>(state_->fans->applyStockPowerProfile(profile,state_->reader));
    }
    std::array<int,3> setCustomCpuPower(const std::array<int,3>& values)override{
        static_cast<void>(hpbsd::customPowerMilliwatts(values));
        std::lock_guard lock(state_->hardware);SYSTEM_POWER_STATUS supply{};
        if(!admitted()||state_->paused||state_->stopping||!state_->fans||!GetSystemPowerStatus(&supply)||supply.ACLineStatus!=1)throw std::runtime_error("HPBSD: Custom requires an active admitted controller and external supply");
        static_cast<void>(state_->fans->applyCustomPower(values,state_->reader));return values;
    }
    void setChargeThresholds(int,int)override{throw std::runtime_error("HPBSD: no battery charge controller");}
    void setFnLock(bool)override{throw std::runtime_error("HPBSD: Fn lock unavailable");}
};
}
std::optional<CpuPowerChoice> savedHpbsdPowerChoice(){
    const auto path=portableSettingsPath(preferenceName);
    if(!std::filesystem::exists(path))return {};
    if(std::filesystem::file_size(path)>128)throw std::runtime_error("HPBSD: oversized power preference");
    std::ifstream file(path);std::string tag,kind,extra;int version{};CpuPowerChoice choice;
    if(!(file>>tag>>version)||tag!="HPBSD_POWER_MODE"||(version!=1&&version!=2))throw std::runtime_error("HPBSD: invalid power preference");
    if(version==1){if(!(file>>choice.baseProfile))throw std::runtime_error("HPBSD: missing native profile");}
    else{
        if(!(file>>kind>>choice.baseProfile)||(kind!="stock"&&kind!="custom"))throw std::runtime_error("HPBSD: invalid power mode");
        choice.custom=kind=="custom";
        if(choice.custom){if(!(file>>choice.values[0]>>choice.values[1]>>choice.values[2]))throw std::runtime_error("HPBSD: missing Custom limits");static_cast<void>(hpbsd::customPowerMilliwatts(choice.values));}
    }
    if(choice.baseProfile<0||choice.baseProfile>1||(file>>extra))throw std::runtime_error("HPBSD: invalid native base/trailing preference data");
    return choice;
}
std::optional<int> savedHpbsdPowerProfile(){
    const auto choice=savedHpbsdPowerChoice();return choice?std::optional<int>(choice->baseProfile):std::nullopt;
}
void saveHpbsdPowerProfile(int profile){
    static_cast<void>(hpbsd::stockPower(profile));if(savedHpbsdPowerChoice()==CpuPowerChoice{profile,false,{}})return;
    std::string error;if(!savePortableText(portableSettingsPath(preferenceName),"HPBSD_POWER_MODE 2 stock "+std::to_string(profile)+"\n",error))throw std::runtime_error("HPBSD: power preference not saved: "+error);
}
void saveHpbsdCustomPower(const std::array<int,3>& values,int baseProfile){
    static_cast<void>(hpbsd::customPowerMilliwatts(values));static_cast<void>(hpbsd::stockPower(baseProfile));
    if(savedHpbsdPowerChoice()==CpuPowerChoice{baseProfile,true,values})return;
    std::string error;const auto text="HPBSD_POWER_MODE 2 custom "+std::to_string(baseProfile)+" "+std::to_string(values[0])+" "+std::to_string(values[1])+" "+std::to_string(values[2])+"\n";
    if(!savePortableText(portableSettingsPath(preferenceName),text,error))throw std::runtime_error("HPBSD: Custom preference not saved: "+error);
}
std::unique_ptr<LaptopControl> createHpbsdPowerControl(){auto state=runtime();return state?std::make_unique<HpbsdPowerControl>(std::move(state)):nullptr;}
void requestHpbsdConfigurationRestore(const std::string& reason){
    if(!admitted())return;
    try{auto state=runtime();state->paused=false;state->configuration->request(reason=="resume"?"power_resume":reason);}
    catch(const std::exception& error){logMessage("hpbsd_power",std::string("Configuration restore unavailable: ")+error.what());}
}
void suspendHpbsdPower(){std::shared_ptr<PowerRuntime> state;{std::lock_guard lock(runtimeGate);state=activeRuntime;}if(state){state->paused=true;state->configuration->suspend();}}
void shutdownHpbsdPower(){
    std::shared_ptr<PowerRuntime> state;{std::lock_guard lock(runtimeGate);state=activeRuntime;}
    if(state){state->stopping=true;state->configuration.reset();}
    {std::lock_guard lock(runtimeGate);if(activeRuntime==state)activeRuntime.reset();}
}
}
