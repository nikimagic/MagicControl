#include "MagicControl/LaptopChargeResource.h"
#include "MagicControl/BatteryUiPolicy.h"
#include <future>
#include <iostream>
#include <mutex>
#include <vector>
#if defined(_WIN32)
#include "../../../src/platform/windows/BatterySettingsWindows.h"
#include "../../../src/platform/windows/SettingsActionWindows.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#endif
namespace {
using namespace mc;using namespace std::chrono_literals;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Shared {
    std::recursive_mutex gate;std::vector<std::string> events;
    LaptopControlSnapshot state;Shared(){state.chargeWritable=true;state.chargeStart=80;state.chargeEnd=95;}
};
struct Native final:LaptopControl {
    std::shared_ptr<Shared> data;bool failRead{},failWrite{};std::string_view policy{"huawei-hlyl-wxx9"};
    explicit Native(std::shared_ptr<Shared> shared=std::make_shared<Shared>()):data(std::move(shared)){}
    std::string_view chargePolicyId()const noexcept override{return policy;}
    void withChargePolicyTransaction(const std::function<void()>& action)override{std::lock_guard lock(data->gate);action();}
    LaptopControlSnapshot read()override{std::lock_guard lock(data->gate);data->events.push_back("read");if(failRead)throw std::runtime_error("read failed");return data->state;}
    void setChargeThresholds(int start,int end)override{
        std::lock_guard lock(data->gate);data->events.push_back("write:"+std::to_string(start)+"/"+std::to_string(end));
        if(failWrite)throw std::runtime_error("write/readback failed");
        if(start<0||end<0||start>end||end>100)throw std::runtime_error("native bounds");
        data->state.chargeStart=start;data->state.chargeEnd=end;
    }
    void setFnLock(bool)override{throw std::logic_error("unexpected key operation");}
};
}
int main(){try{
    Native control;int selected=80,saved=-1,capacitySaves{};
    NativeBatteryActions actions{
        [&](int fallback){control.data->events.push_back("select:"+std::to_string(fallback));return selected;},
        [&](int value){control.data->events.push_back("save:"+std::to_string(value));saved=value;},
        [&]{control.data->events.push_back("capacity");++capacitySaves;},{}};
    for(const int end:{100,95,85,60,10}){
        control.data->events.clear();saved=-1;const int before=capacitySaves;
        const int expected=end==10?5:end==60?55:selected;
        executeBatteryResource(control,{BatteryWrite::Upper,end},actions);
        check(control.data->state.chargeStart==expected&&control.data->state.chargeEnd==end,"model upper/lower policy changed");
        check(control.data->events.front()=="read"&&capacitySaves==before+1&&saved==(end==10?5:-1),"upper did not confirm before save or overwrote selected restart");
    }
    control.data->state.chargeEnd=95;control.data->events.clear();saved=-1;
    executeBatteryResource(control,{BatteryWrite::Restart,90},actions);
    check(control.data->events==std::vector<std::string>({"read","write:90/95","save:90"})&&saved==90,"restart did not use fresh upper or save after confirmation");
    control.data->events.clear();const int priorCapacity=capacitySaves;saved=-1;
    auto restore=BatteryWriteRequest::native(85,80);restore.origin=BatteryWriteOrigin::Restore;
    executeBatteryResource(control,restore,actions);
    check(control.data->events==std::vector<std::string>({"read","write:80/85"})&&saved==-1&&capacitySaves==priorCapacity,"restore became a new user choice");
    for(int failure=0;failure<4;++failure){
        control.failRead=failure==0;control.failWrite=failure==1;control.data->state.chargeWritable=failure!=2;
        selected=failure==3?95:80;control.data->events.clear();saved=-1;const int before=capacitySaves;
        bool refused{};try{executeBatteryResource(control,{BatteryWrite::Upper,85},actions);}catch(const std::exception&){refused=true;}
        check(refused&&saved==-1&&capacitySaves==before,"failed read/write/admission persisted success");
        if(failure!=1)check(control.data->events.size()<=2,"failure before writer changed hardware");
    }
    control.failRead=false;control.failWrite=false;control.data->state.chargeWritable=true;selected=80;
    control.data->events.clear();executeBatteryResource(control,BatteryWriteRequest::pair(0,100),{});
    check(control.data->events==std::vector<std::string>{"write:0/100"},"expert pair changed native disabled normalization or added a policy read");
    control.data->events.clear();control.policy="future-oem";bool refused{};
    try{executeBatteryResource(control,{BatteryWrite::Upper,95},actions);}catch(const std::exception&){refused=true;}
    check(refused&&control.data->events.empty(),"unknown policy borrowed Honor admission");control.policy="huawei-hlyl-wxx9";
    for(auto request:{BatteryWriteRequest{},BatteryWriteRequest{BatteryWrite::Policy},BatteryWriteRequest{BatteryWrite::Protection,1},BatteryWriteRequest{static_cast<BatteryWrite>(99)}}){
        control.data->events.clear();refused=false;try{executeBatteryResource(control,request,actions);}catch(const std::exception&){refused=true;}
        check(refused&&control.data->events.empty(),"invalid or foreign battery operation reached native transport");
    }
    auto foreign=BatteryWriteRequest{BatteryWrite::Upper,95};foreign.requestedPolicy=BatteryEcPolicy::Platform7Percent;
    refused=false;try{executeBatteryResource(control,foreign,actions);}catch(const std::exception&){refused=true;}
    check(refused&&control.data->events.empty(),"Uniwill ROM policy leaked into native battery");
    auto failStore=actions;failStore.rememberRestart=[](int){throw std::runtime_error("save failed");};control.data->events.clear();
    refused=false;try{executeBatteryResource(control,BatteryWriteRequest::native(95,90),failStore);}catch(const std::exception&){refused=true;}
    check(refused&&control.data->events==std::vector<std::string>({"read","write:90/95"}),"save failure did not report an already applied command");
    // The same lock covers read/decision/write/save, including competing adapters.
    auto shared=std::make_shared<Shared>();Native upper(shared),lower(shared);
    std::promise<void> entered,release,attempted;auto inside=entered.get_future();auto released=release.get_future().share();auto trying=attempted.get_future();
    auto first=std::async(std::launch::async,[&]{return executeBatteryResource(upper,{BatteryWrite::Upper,85},
        {[&](int){entered.set_value();released.wait();return 75;},[](int){},[]{},{}});});
    const bool ready=inside.wait_for(3s)==std::future_status::ready;
    auto second=std::async(std::launch::async,[&]{attempted.set_value();return executeBatteryResource(lower,{BatteryWrite::Restart,80},{[](int){return 80;},[](int){},[]{},{}});});
    const bool started=trying.wait_for(3s)==std::future_status::ready;
    const bool blocked=second.wait_for(50ms)==std::future_status::timeout;release.set_value();first.get();second.get();
    check(ready&&started&&blocked&&shared->events==std::vector<std::string>({"read","write:75/85","read","write:80/85"}),"restart raced a fresh upper transaction");
    for(const auto [platform,rom,policy]:{std::array<int,3>{4,255,0},std::array<int,3>{7,4,1},std::array<int,3>{7,255,2}}){
        const auto patch=observedBatteryPreferences(0x80|95,platform,rom);
        check(patch[DeviceSetting::ChargeLimit]==95&&patch[DeviceSetting::ChargeAuto]==policy&&patch[DeviceSetting::BatteryCardView]==1,"confirmed projection lost mask/policy meaning");
    }
#if defined(_WIN32)
    setLoggingEnabled(false);
    const auto preferences=batteryPreferencesPath("huawei-hlyl-wxx9");
    check(!std::filesystem::exists(settingsReadPath(preferences)),"fixture must not have personal native settings");
    struct Cleanup{std::filesystem::path file;~Cleanup(){std::error_code error;std::filesystem::remove(file,error);}} cleanup{preferences};
    Native gui;windows::BatterySettingsContext context{};context.laptop=&gui;context.profile="huawei-hlyl-wxx9";
    windows::SettingsActionServices services;services.battery=[&]{return context;};
    windows::executeSettingsAction({windows::SettingsActionKind::BatteryUpper,1},services);
    check(gui.data->state.chargeEnd==95&&gui.data->state.chargeStart==90,"GUI preset did not enter native policy");
    windows::executeBatteryResource(context,{BatteryWrite::Restart,80});
    windows::executeBatteryResource(context,{BatteryWrite::Upper,85});
    check(gui.data->state.chargeStart==80&&gui.data->state.chargeEnd==85,"CLI/native entry did not retain saved restart");
    gui.data->events.clear();
    const auto selectionResult=windows::executeSettingsAction({windows::SettingsActionKind::BatteryUpper,1},services);
    check(selectionResult.batteryView==1&&gui.data->state.chargeEnd==95&&gui.data->state.chargeStart==80,
        "percentage action did not use the shared confirmed native path");
    gui.data->events.clear();
    bool foreignPolicy{};try{windows::executeBatteryResource(context,{BatteryWrite::Policy});}catch(const std::exception&){foreignPolicy=true;}
    check(foreignPolicy&&gui.data->events.empty(),"native context borrowed Uniwill ID policy");
    DevicePreferences stored;std::string error;
    check(loadDevicePreferences(preferences,stored,error)&&stored[DeviceSetting::ChargeRestart]==80&&stored[DeviceSetting::BatteryPercentageEnabled]==1,"confirmed native persistence was not shared");
    int missing{};for(const auto* profile:deviceProfiles()){
        windows::BatterySettingsContext absent;absent.profile=std::string(profile->id);
        for(const auto kind:{BatteryWrite::Upper,BatteryWrite::Restart,BatteryWrite::Policy,BatteryWrite::Automatic,BatteryWrite::Protection,BatteryWrite::Aging,BatteryWrite::Pair}){
            refused=false;try{windows::executeBatteryResource(absent,{kind,95});}catch(const std::exception&){refused=true;}
            check(refused,"catalogue declaration granted a battery writer");
        }++missing;
    }
    std::cout<<"PASS: GUI/CLI native request, saved lower/capacity and "<<missing<<" missing-transport declarations\n";
#endif
    std::cout<<"PASS: native upper/lower/pair, policy isolation, failures-before-save, restore origin and serialized transactions\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
