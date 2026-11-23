#include "../src/platform/windows/SettingsOperationsWindows.h"
#include "../src/platform/windows/SettingsActionWindows.h"
#include "../src/platform/windows/KeysHardwareWindows.h"
#include "../src/platform/windows/FanDeviceActionsWindows.h"
#include "../src/platform/windows/InfoOperationsWindows.h"
#include "../src/platform/windows/SettingsSnapshotWindows.h"
#include "../src/platform/windows/LightingControlWindows.h"
#include "../src/platform/windows/SettingsPersistenceWindows.h"
#include "../src/platform/windows/IntelTelemetryWindows.h"
#include "../src/platform/windows/SettingsPowerSelectionWindows.h"
#include "../src/platform/windows/CustomModeWindows.h"
#include "../src/platform/windows/BatterySettingsWindows.h"
#include "MagicControl/LaptopChargeResource.h"
#include "../src/platform/windows/huawei-wmi/HuaweiWmiGate.h"
#include "../src/platform/windows/EcLoggerWindows.h"
#include "../src/gui/windows/SettingsNotice.h"
#include "../src/gui/common/Localization.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/ResourceCatalog.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/OperationMessages.h"
#include "MagicControl/EcAccess.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <future>
#include <mutex>
#include <stdexcept>
#include <vector>

using mc::BatteryWrite;
using mc::BatteryWriteRequest;
using mc::NativeBatteryActions;

namespace mc::gui {
struct SettingsBackendTestAccess {
    using Factories=SettingsOperationsWindows::BackendFactories;
    static auto acquire(bool preview,bool ec,const Factories& factories){return SettingsOperationsWindows::acquireBackends(preview,ec,factories);}
};
}

struct FakeLaptopPower final:mc::LaptopControl {
    std::vector<int> calls;
    mc::LaptopControlSnapshot read() override{return {};}
    void setChargeThresholds(int,int) override{throw std::runtime_error("unexpected battery write");}
    void setFnLock(bool) override{throw std::runtime_error("unexpected Fn write");}
    void setStockCpuProfile(int profile) override{calls.push_back(profile);if(profile>1)throw std::runtime_error("unsupported native profile");}
};
struct FakeHonorBattery final:mc::LaptopControl {
    std::vector<std::string>& calls;mc::LaptopControlSnapshot state;bool failWrite{};
    std::shared_ptr<std::recursive_mutex> gate;std::shared_ptr<mc::LaptopControlSnapshot> sharedState;
    explicit FakeHonorBattery(std::vector<std::string>& events,std::shared_ptr<std::recursive_mutex> lock=std::make_shared<std::recursive_mutex>()):calls(events),gate(std::move(lock)){state.chargeWritable=true;state.chargeEnd=95;}
    std::string_view chargePolicyId() const noexcept override{return "huawei-hlyl-wxx9";}
    std::string_view customPowerPolicyId() const noexcept override{return "huawei-hlyl-windows-smu";}
    void withChargePolicyTransaction(const std::function<void()>& action) override{std::lock_guard lock(*gate);action();}
    mc::LaptopControlSnapshot read() override{calls.push_back("read");return sharedState?*sharedState:state;}
    void setChargeThresholds(int start,int end) override{
        calls.push_back("write:"+std::to_string(start)+"/"+std::to_string(end));
        if(failWrite)throw std::runtime_error("fake write failure");
        if(sharedState){sharedState->chargeStart=start;sharedState->chargeEnd=end;}
    }
    void setFnLock(bool) override{throw std::runtime_error("unexpected Fn write");}
};
struct FakeUnknownBattery final:mc::LaptopControl {
    int reads{},writes{},turbo{},keyboardLevel{-1},keyboardTimeout{-1};
    std::string_view chargePolicyId() const noexcept override{return "future-oem";}
    mc::LaptopControlSnapshot read() override{++reads;mc::LaptopControlSnapshot s;s.chargeWritable=true;s.chargeEnd=95;return s;}
    void setChargeThresholds(int,int) override{++writes;}
    void setNativeTurbo(bool) override{++turbo;}
    void setKeyboardLevel(int value) override{keyboardLevel=value;}
    void setKeyboardTimeout(int value) override{keyboardTimeout=value;}
    void setFnLock(bool) override{}
};
struct FakeKeyControl final:mc::LaptopControl {
    int writes{},reads{};bool fn{};
    mc::LaptopControlSnapshot read() override{++reads;mc::LaptopControlSnapshot s;s.fnLock=fn;return s;}
    void setChargeThresholds(int,int) override{throw std::runtime_error("unexpected battery write");}
    void setFnLock(bool value) override{++writes;fn=value;}
};
struct FakeNativeFans final:mc::windows::NativeFixedFans {
    mc::windows::NativeFixedFanState state;std::vector<std::string> calls;bool failWrite{},failRead{};
    mc::windows::NativeFixedFanState read() override{calls.push_back("read");if(failRead)throw std::runtime_error("read error");return state;}
    void setFixed(const std::array<int,2>& target) override{calls.push_back("fixed");if(failWrite)throw std::runtime_error("write error");state.manual=true;state.targetPercent=target;}
    mc::windows::NativeFanAutoResult setAutomatic() override{calls.push_back("auto");if(failWrite)throw std::runtime_error("write error");state.manual=false;return mc::windows::NativeFanAutoResult::Applied;}
    void setCurves(const mc::NativeFanCurves& curves) override{calls.push_back("curves");if(failWrite)throw std::runtime_error("write error");
        state.manual=true;state.curves=mc::NativeFanCurveState{};state.curves->draft=curves;state.curves->appliedCurve=true;}
};
struct FakeSettingsPreviewView final:mc::gui::SettingsOperationsWindows::View {
    int renders{},updates{},fields{},notices{};
    mc::gui::osd::Message notice(const mc::windows::SettingsAction&)const override{return {};}
    void statusChanged(const std::wstring&)override{}
    bool readPowerFields(std::array<int,3>&)override{++fields;return true;}
    void renderSelection()override{++renders;}
    void updateButtons()override{++updates;}
    void loggerChanged()override{}
    std::uint64_t beginNotice(const mc::gui::osd::Message&)override{++notices;return 1;}
    void completeNotice(std::uint64_t,mc::gui::osd::Message)override{++notices;}
};

int main(int argc,char** argv){
    if(argc==3&&std::string_view(argv[1])=="--hwmi-gate-child"){
        const std::string suffix(argv[2]);const std::wstring id(suffix.begin(),suffix.end());
        const auto base=L"Local\\MagicControl.HwmiGate.ProcessTest."+id;
        const auto attemptName=base+L".attempt",acquiredName=base+L".acquired";
        HANDLE attempt=OpenEventW(EVENT_MODIFY_STATE,FALSE,attemptName.c_str());
        HANDLE acquired=OpenEventW(EVENT_MODIFY_STATE,FALSE,acquiredName.c_str());
        if(!attempt||!acquired){if(attempt)CloseHandle(attempt);if(acquired)CloseHandle(acquired);return 2;}
        int result=0;
        try{if(!SetEvent(attempt))result=3;else{mc::huawei::WmiGate gate(base.c_str());if(!SetEvent(acquired))result=4;}}
        catch(...){result=5;}
        CloseHandle(attempt);CloseHandle(acquired);return result;
    }
using namespace mc::gui;
    using D=SettingsDispatch::Decision;
    SettingsDispatch c;
    auto check=[](bool ok,const char* what){if(!ok){std::cerr<<what<<'\n';std::exit(1);}};
    if(argc==2&&std::string_view(argv[1])=="--preview-owner"){
        using namespace mc::windows;
        FakeSettingsPreviewView view;int workers=0,callbacks=0;
        mc::gui::SettingsOperationsWindows model(view,nullptr,[&](std::string){++callbacks;},nullptr,mc::BoardDescriptor{},true,
            [&](std::function<void()> task)->std::thread{++workers;return std::thread(std::move(task));});
        model.readState();
        for(auto kind:{SettingsActionKind::StockPower,SettingsActionKind::BatteryUpper,SettingsActionKind::LightingLevel,
                       SettingsActionKind::LightingEffect,SettingsActionKind::LightingColor,SettingsActionKind::LightingNative})
            model.submit(0,{},SettingsAction{kind,1},L"preview");
        model.applyPowerFromFields();model.selectCustomPreset(2);model.applyVoltageMode(1);model.applyBatteryAging(true);
        model.editPowerDraftField(0,L"42");model.selectBatteryView(mc::BatteryCardView::Capacity);
        check(model.previewState().preset==2&&model.previewState().batteryMode==1&&model.previewState().batteryAging==1,
              "preview owner updates local selection");
        check(workers==0&&view.fields==0&&view.notices==0,"preview owner reached worker, hardware draft or notice");
        check(!model.hasQueuedCommand()&&view.renders>0&&view.updates>0,"preview owner did not render locally");
        (void)callbacks;std::cout<<"settings preview owner checks passed\n";return 0;
    }
    {
        using namespace mc::windows;using S=mc::DeviceSetting;
        mc::DevicePreferences saved;
        check(!savedBatteryWriteRequest(saved),"restore without saved battery intent sends no command");
        for(const int limit:{10,60,85,95,100})for(const int policy:{0,1,2}){
            saved[S::ChargeLimit]=limit;saved[S::ChargeAuto]=policy;
            const auto request=savedBatteryWriteRequest(saved);
            check(request&&request->kind==BatteryWrite::Upper&&request->value==limit&&request->restart==-1&&
                !request->platform7&&!request->requestedPolicy,
                "restore preserves upper without letting legacy ChargeAuto override actual platform or disable the percentage gate");
        }
        saved[S::BatteryPercentageEnabled]=0;
        const auto request=savedBatteryWriteRequest(saved);
        check(request&&request->kind==BatteryWrite::View&&request->value==0,
            "native view restores through the shared off command instead of replaying an old percentage");
        BatteryWriteRequest interactive{BatteryWrite::Upper,95};
        check(!interactive.requestedPolicy,"ordinary button/CLI requests preserve observed platform");
    }
    {
        int applied=0,finalized=0;bool rejected=false;mc::ApplicationPreferences previous{};
        try{mc::windows::applyKeyPreferenceTransaction(mc::KeyAction::Fn,true,previous,
            [&](const mc::ApplicationPreferences&){++applied;},[&]{++finalized;});}
        catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&applied==0&&finalized==0,"non-preference KEYs cannot mutate application preferences or their storage");
    }
    {
        using namespace mc::windows;
        BatterySettingsContext missing;
        for(const auto kind:{BatteryWrite::Upper,BatteryWrite::Restart,BatteryWrite::Policy,BatteryWrite::Automatic,BatteryWrite::Protection,BatteryWrite::NativeTurbo}){
            bool rejected=false;try{executeBatteryResource(missing,{kind,95});}catch(const std::runtime_error&){rejected=true;}
            check(rejected,"battery resource without its explicit backend cannot fall through to another adapter");
        }
        FakeUnknownBattery unknown;missing.laptop=&unknown;
        BatteryWriteRequest turbo{BatteryWrite::NativeTurbo};turbo.enabled=true;bool rejected=false;
        try{executeBatteryResource(missing,turbo);}catch(const std::runtime_error&){rejected=true;}
        check(rejected&&unknown.turbo==0&&unknown.writes==0,"native battery policy never borrows the Honor ABI on an unknown typed backend");
        CustomModeContext power;
        for(const auto kind:{PowerWrite::Custom,PowerWrite::Preset,PowerWrite::Automatic,PowerWrite::Economy,PowerWrite::Allowed}){
            rejected=false;try{executePowerResource(power,{kind,{10,20,30},1});}catch(const std::runtime_error&){rejected=true;}
            check(rejected,"power resource without its explicit backend cannot pick another device");
        }
        power.honor=true;power.profileId="future-oem";rejected=false;
        try{executePowerResource(power,{PowerWrite::Custom,{10,20,30}});}catch(const std::runtime_error&){rejected=true;}
        check(rejected,"resource key does not authorize an unconfirmed Honor power policy");
        mc::gui::LightingControlContext rgb;rgb.laptop=&unknown;rejected=false;
        try{mc::gui::executeLightingResource(rgb,mc::LightingWriteRequest::singleZone({},mc::LightingWriteOrigin::Explicit));}catch(const std::exception&){rejected=true;}
        check(rejected&&unknown.keyboardLevel==-1,"raw RGB never falls through to an unrelated native brightness adapter");
        rejected=false;try{executeBatteryResource(missing,{static_cast<BatteryWrite>(99)});}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"unknown battery operation rejected before adapter invocation");
        power={};rejected=false;try{executePowerResource(power,{static_cast<PowerWrite>(99)});}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"unknown power operation rejected before adapter invocation");
    }
    {
        using namespace mc::windows;using K=SettingsActionKind;
        const std::array kinds{K::BatteryUpper,K::BatteryRestart,K::BatteryPolicy,K::BatteryVoltage,K::BatteryView,K::StockPower,
            K::LightingLevel,K::LightingEffect,K::LightingColor,K::LightingCustomColor,K::LightingNative,
            K::LightingTimeout,K::LightingCycle,K::LightingZone,K::CustomPreset,K::CustomEditor};
        std::vector<mc::ResourceControlKey> keys;
        for(const auto kind:kinds){const auto key=settingsResourceControlKey(SettingsAction{kind});
            check(!key.instance.empty()&&!key.operation.empty()&&settingsResourceRouteRegistered(key)&&std::find(keys.begin(),keys.end(),key)==keys.end(),"SETs resource routes are registered and distinct");keys.push_back(key);}
        check(settingsResourceControlKey(SettingsAction{K::BatteryUpper}).kind==mc::ResourceKind::Battery&&
            settingsResourceControlKey(SettingsAction{K::StockPower}).kind==mc::ResourceKind::Processor&&
            settingsResourceControlKey(SettingsAction{K::LightingLevel}).kind==mc::ResourceKind::Lighting,
            "SETs actions target the correct resource kind");
        SettingsAction logger{K::EcLogger};logger.logger=mc::gui::EcLoggerRequest{0x770,0,0,false};
        const auto read=settingsResourceControlKey(logger);logger.logger->writing=true;const auto write=settingsResourceControlKey(logger);
        check(read.kind==mc::ResourceKind::Other&&read.instance=="ec"&&read.operation=="ec.read"&&write.operation=="ec.write"&&read!=write&&settingsResourceRouteRegistered(read)&&settingsResourceRouteRegistered(write),
            "EC logger read and write routes remain distinct");
        check(!settingsResourceRouteRegistered({mc::ResourceKind::Fan,"active","charge.upper"}),"wrong resource does not inherit another resource route");
    }
    {
        using namespace mc::windows;mc::KeySnapshot hardware;hardware.value.fill(-1);
        hardware.value[0]=1;hardware.available[0]=true;hardware.detail[0]="old";
        hardware.value[6]=0;hardware.available[6]=true;hardware.detail[6]="wifi detail";
        hardware.value[5]=1;hardware.available[5]=true;hardware.detail[5]="native firmware";
        const auto result=composeKeysSnapshot(hardware,true);
        check(result.value[1]==1&&result.value[5]==1&&result.available[1]&&result.available[5]&&result.detail[5]=="native firmware",
            "KEYs keeps OSD local and Copilot from its actual hardware observation");
        check(result.detail[0].find("Windows")!=std::string::npos&&result.value[6]==0&&result.detail[6]=="wifi detail"&&
            result.value[2]==-1&&!result.available[2],"KEYs projection retains device observations and unknown values");
        using P=mc::KeyCommandPlan;
        check(mc::planKeyCommand("unknown",1,true,true,false,false,false)==P::LocalPreference&&
            mc::planKeyCommand("unknown",5,true,true,true,true,false)==P::UnsupportedRoute&&
            mc::planKeyCommand("unknown",5,false,true,true,true,false)==P::Queue,
            "OSD stays local; Copilot needs actual firmware and uses the hardware queue");
        check(mc::planKeyCommand("unknown",10,true,true,false,false,false)==P::Start&&
            mc::planKeyCommand("unknown",10,true,true,true,true,false)==P::Queue,
            "system microphone remains available on unknown devices and queues behind refresh");
        check(mc::planKeyCommand("unknown",2,false,true,false,false,false)==P::UnsupportedRoute&&
            mc::planKeyCommand("unknown",10,false,false,false,false,false)==P::Unavailable&&
            mc::planKeyCommand("unknown",10,false,true,true,false,false)==P::Busy&&
            mc::planKeyCommand("unknown",10,false,true,true,true,true)==P::Busy,
            "unknown hardware route, unavailable row and busy queue remain rejected");
        FakeKeyControl control;KeysHardwareContext context{"huawei-hlyl-wxx9",false,nullptr,&control,nullptr};
        applyKeyHardware(context,mc::KeyAction::Fn,true);
        check(control.writes==1&&control.reads==1&&control.fn,"resource-routed Fn calls the original typed setter and readback");
        context.preview=true;bool refused=false;try{applyKeyHardware(context,mc::KeyAction::Fn,false);}catch(const std::runtime_error&){refused=true;}
        check(refused&&control.writes==1&&control.reads==1,"preview cannot write through KEYs resource route");
        context.preview=false;context.profile="unknown";refused=false;
        try{applyKeyHardware(context,mc::KeyAction::Fn,false);}catch(const std::runtime_error&){refused=true;}
        check(refused&&control.writes==1&&control.reads==1,"unknown board cannot inherit Honor key commands");
    }
    {
        using namespace mc::windows;const auto widen=[](const std::string& s){return std::wstring(s.begin(),s.end());};
        FakeNativeFans fake;fake.state.writable=true;fake.state.minimumSupported=true;fake.state.minimumPercent={36,38};
        mc::FanWriteRequest request{mc::FanResourceOperation::Manual};request.target=std::array<int,2>{40,45};
        auto applied=executeFanResource({nullptr,&fake},request);
        check(applied.success&&applied.native&&applied.native->targetPercent==std::array<int,2>{40,45}&&
            fake.calls==std::vector<std::string>({"fixed","read"}),"native FAN entry preserves command and readback order");
        fake.calls.clear();fake.failWrite=true;request.target=std::array<int,2>{50,55};
        applied=executeFanResource({nullptr,&fake},request);
        check(!applied.success&&fake.calls==std::vector<std::string>({"fixed","read"})&&applied.message==L"write error", "failed native command retains readback and its cause");
        fake.calls.clear();fake.failWrite=false;request.operation=mc::FanResourceOperation::Default;
        applied=executeFanResource({nullptr,&fake},request);
        check(applied.success&&fake.calls==std::vector<std::string>({"auto","read"}),"native Default checks automatic readback");
        for(const auto operation:{mc::FanResourceOperation::Maximum,mc::FanResourceOperation::Minimum}){
            fake.calls.clear();request.operation=operation;applied=executeFanResource({nullptr,&fake,nullptr,{},true},request);
            check(applied.success&&fake.state.targetPercent==(operation==mc::FanResourceOperation::Maximum?std::array<int,2>{100,100}:std::array<int,2>{36,38}),"native Max/Min retain actual model targets");
        }
        mc::NativeFanCurves curves{};curves[0][0]={20,5};curves[0][7]={100,100};curves[1][2]={51,43};
        const auto original=curves;fake.calls.clear();request.operation=mc::FanResourceOperation::Manual;request.profiles=mc::FanCurveProfiles{};request.profiles->curves[0]=curves;
        applied=executeFanResource({nullptr,&fake,nullptr,{},true},request);
        check(applied.success&&curves==original&&fake.state.curves&&fake.state.curves->draft==original&&fake.calls==std::vector<std::string>({"curves","read"}),"native request preserves every curve point");

    }
    {
        using namespace mc;std::vector<std::string> events;bool partial=false,cancelDuringCollect=false;
        std::promise<void> entered;
        InfoOperationServices services;
        services.collect=[&](std::stop_token token){events.push_back("collect");HardwareInventory value;value.readAt="fixture";
            if(cancelDuringCollect){entered.set_value();while(!token.stop_requested())std::this_thread::yield();}return value;};
        services.save=[&](const HardwareInventory&,std::string&){events.push_back("save");return true;};
        services.dump=[&](const InfoDumpResourceRequest&){events.push_back("dump");InfoDumpResourceResult result;
            result.report.saved=!partial;result.report.message=partial?"partial":"dump";
            if(partial)result.vbios.vbiosSaved=true;return result;};
        services.prepare=[&](std::stop_token)->std::filesystem::path{events.push_back("prepare");throw std::runtime_error("fixture");};
        services.launch=[](const auto&,const auto&,auto){};
        const auto wait=[&](const std::shared_ptr<InfoOperation>& job){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!job->done()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
            const auto* result=job->poll();check(result!=nullptr,"INFO publishes bounded worker completion");return *result;
        };
        InfoOperationRequest request;
        auto result=wait(InfoOperation::start(request,services));
        check(result.success&&events==std::vector<std::string>({"collect","save"})&&result.message.find("fixture")!=std::string::npos,
            "INFO collects then saves and publishes completion");
        events.clear();cancelDuringCollect=true;auto job=InfoOperation::start(request,services);
        check(entered.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready,"INFO entered fixture collector");
        job->cancel();result=wait(job);cancelDuringCollect=false;
        check(!result.success&&result.cancelled&&events==std::vector<std::string>{"collect"},"cancelled INFO does not save inventory");
        events.clear();request.operation=InfoResourceOperation::EcDump;result=wait(InfoOperation::start(request,services));
        check(result.success&&events==std::vector<std::string>{"dump"}&&result.message=="dump","INFO dump routes only to supplied service");
        events.clear();request.operation=InfoResourceOperation::VbiosDump;partial=true;result=wait(InfoOperation::start(request,services));
        check(result.success&&result.vbios.outcome()=="completed"&&result.message=="partial"&&events==std::vector<std::string>{"dump"},
            "INFO treats a saved ordinary VBIOS as complete");
        events.clear();request.operation=InfoResourceOperation::PawnIoPrepare;result=wait(InfoOperation::start(request,services));
        check(!result.success&&events==std::vector<std::string>{"prepare"}&&result.message.find("fixture")!=std::string::npos,
            "INFO worker publishes preparation failure");
    }
    {
        using namespace mc::windows;FakeUnknownBattery unknown;FakeLaptopPower stock;
        int batteryCalls=0,powerCalls=0,lightingCalls=0,profileCalls=0,persistCalls=0;
        SettingsActionServices services{};
        services.battery=[&]{++batteryCalls;BatterySettingsContext c{};c.laptop=&unknown;return c;};
        services.lighting=[&]{++lightingCalls;mc::gui::LightingControlContext c{};c.laptop=&unknown;return c;};
        services.custom=[&]{++powerCalls;++profileCalls;CustomModeContext c{};c.laptop=&stock;c.profileId="fixture-profile";
            c.fanDefaultLocked=[]{throw std::runtime_error("unexpected fan query");return false;};
            c.onBattery=[]{throw std::runtime_error("unexpected battery query");return false;};
            c.persistStockChoice=[&](std::string_view profile,bool active){check(profile=="fixture-profile"&&!active&&!stock.calls.empty(),"stock persist follows typed setter");++persistCalls;};return c;};
        executeSettingsAction({SettingsActionKind::LightingLevel,2},services);
        executeSettingsAction({SettingsActionKind::LightingTimeout,17},services);
        check(unknown.keyboardLevel==2&&unknown.keyboardTimeout==17&&lightingCalls==2&&batteryCalls==0&&powerCalls==0&&profileCalls==0,
            "lighting actions use only their typed service context");
        executeSettingsAction({SettingsActionKind::StockPower,1},services);
        check(stock.calls==std::vector<int>{0}&&powerCalls==1&&profileCalls==1&&persistCalls==1&&batteryCalls==0,
            "stock action delegates to native profile and then persists");
        bool refused=false;try{executeSettingsAction({SettingsActionKind::StockPower,3},services);}catch(const std::runtime_error&){refused=true;}
        check(refused&&stock.calls==std::vector<int>({0,2})&&persistCalls==1,"failed stock writer cannot persist success");
        refused=false;try{executeSettingsAction({SettingsActionKind::BatteryUpper,1},services);}catch(const std::runtime_error&){refused=true;}
        check(refused&&batteryCalls==1&&unknown.reads==0&&unknown.writes==0,
            "unknown typed battery cannot inherit Honor policy through dispatcher");
        int customCalls=0;services.custom=[&] {++customCalls;CustomModeContext context{};context.profileId="future-oem";context.unsupportedTyped=true;return context;};
        refused=false;try{executeSettingsAction({SettingsActionKind::CustomPreset,0},services);}catch(const std::runtime_error&){refused=true;}
        check(refused&&customCalls==1,"unsupported custom preset is rejected before loading or writing settings");
        SettingsAction editor{SettingsActionKind::CustomEditor};refused=false;
        try{executeSettingsAction(editor,services);}catch(const std::invalid_argument&){refused=true;}
        check(refused&&customCalls==1,"missing editor plan does not obtain a backend");
        editor.customPlan=CustomEditorPlan{CustomClickDecision::ApplyHonor,CustomPrepareError::Fields,{10,20,30}};refused=false;
        try{executeSettingsAction(editor,services);}catch(const std::invalid_argument&){refused=true;}
        check(refused&&customCalls==1,"invalid editor plan cannot reach a backend");
        editor.customPlan=CustomEditorPlan{CustomClickDecision::ApplyHonor,CustomPrepareError::None,{10,20,30}};refused=false;
        try{executeSettingsAction(editor,services);}catch(const std::runtime_error&){refused=true;}
        check(refused&&customCalls==2,"unknown typed backend refuses valid-looking custom request");
        bool touched=false;std::optional<mc::EcByteObservation> observation;
        services.loggerTouched=&touched;services.loggerObservation=&observation;
        SettingsAction logger{SettingsActionKind::EcLogger};refused=false;
        try{executeSettingsAction(logger,services);}catch(const std::invalid_argument&){refused=true;}
        check(refused&&!touched&&batteryCalls==1,"incomplete logger request cannot touch backend or state");
        logger.logger=mc::gui::EcLoggerRequest{0x770,0,0,false};refused=false;
        try{executeSettingsAction(logger,services);}catch(const std::runtime_error&){refused=true;}
        check(refused&&touched&&!observation&&batteryCalls==2&&unknown.reads==0&&unknown.writes==0,
            "logger marks attempted observation and refuses unavailable EC");
        const auto before=batteryCalls+powerCalls+lightingCalls+profileCalls;
        refused=false;try{executeSettingsAction({static_cast<SettingsActionKind>(999)},services);}catch(const std::invalid_argument&){refused=true;}
        check(refused&&before==batteryCalls+powerCalls+lightingCalls+profileCalls,"unknown action touches no service");
    }
    {
        using namespace mc::windows;FakeUnknownBattery unknown;std::vector<std::string> events;FakeHonorBattery honor(events);
        check(!honorCustomPowerBackend(nullptr)&&!honorCustomPowerBackend(&unknown)&&honorCustomPowerBackend(&honor),
            "only explicit Honor custom-power policy selects HLYL SMU");
        auto audited=mc::auditedLaptopControl(std::make_unique<FakeHonorBattery>(events));
        check(honorCustomPowerBackend(audited.get()),"audited controller forwards independent power policy identity");
        int queries=0;CustomClickContext click{};click.unsupportedTyped=true;
        click.fanDefaultLocked=[&]{++queries;return false;};
        check(decideCustomClick(click)==CustomClickDecision::Reject&&queries==0,
            "unknown typed backend cannot open Honor or EC custom editor");
        CustomModeContext unsupported{};unsupported.profileId="future-oem";unsupported.unsupportedTyped=true;
        bool refused=false;try{static_cast<void>(executePowerResource(unsupported,{PowerWrite::CustomEditor,{10,20,30},0}));}catch(const std::runtime_error&){refused=true;}
        check(refused,"unknown typed backend cannot invoke custom manual writer");
        refused=false;try{static_cast<void>(executePowerResource(unsupported,{PowerWrite::Preset,{},0}));}catch(const std::runtime_error&){refused=true;}
        check(refused,"unknown typed backend cannot load or apply Honor presets");
        unsupported.unsupportedTyped=false;unsupported.honor=true;refused=false;
        try{static_cast<void>(executePowerResource(unsupported,{PowerWrite::Preset,{},0}));}catch(const std::runtime_error&){refused=true;}
        check(refused,"wrong profile cannot enter Honor custom branch");
        mc::gui::SettingsSnapshot state;state.apu.stapmLimitWatts=25;state.apu.slowLimitWatts=25;state.apu.fastLimitWatts=30;
        PowerSelectionContext selection{state,{25,25,30},4,4,true,false,false,false,true,false,false,false,false};
        check(selectedPowerMode(selection)==-1,"unsupported OEM does not display Honor custom selection");
        mc::gui::LightingControlContext light{};light.laptop=&unknown;
        mc::gui::executeLightingResource(light,{mc::LightingWrite::Level,2});mc::gui::executeLightingResource(light,{mc::LightingWrite::Timeout,17});
        check(unknown.keyboardLevel==2&&unknown.keyboardTimeout==17,
            "generic typed lighting remains delegated to its own backend");
    }
    {
        using namespace mc::windows;FakeUnknownBattery unknown;std::vector<std::string> events;FakeHonorBattery honor(events);
        check(!honorBatteryBackend(nullptr)&&!honorBatteryBackend(&unknown)&&honorBatteryBackend(&honor),
            "only explicitly identified Honor backend selects Honor battery policy");
        BatterySettingsContext context{};context.laptop=&unknown;bool refused=false;
        try{executeBatteryResource(context,{BatteryWrite::Preset,1});}catch(const std::runtime_error&){refused=true;}
        check(refused&&unknown.reads==0&&unknown.writes==0,"unknown typed upper cannot use Honor policy");
        refused=false;try{executeBatteryResource(context,{BatteryWrite::Restart,90});}catch(const std::runtime_error&){refused=true;}
        check(refused&&unknown.reads==0&&unknown.writes==0,"unknown typed restart cannot use Honor policy");
        refused=false;try{executeBatteryResource(context,{BatteryWrite::PolicyButton});}catch(const std::runtime_error&){refused=true;}
        check(refused&&unknown.turbo==0,"unknown typed backend cannot inherit Honor turbo policy");
        int callbacks=0;NativeBatteryActions actions{[&](int){++callbacks;return 80;},[&](int){++callbacks;},[&]{++callbacks;},{}};
        refused=false;try{mc::executeBatteryResource(unknown,{BatteryWrite::Upper,95},actions);}catch(const std::runtime_error&){refused=true;}
        check(refused&&callbacks==0&&unknown.writes==0,"Honor adapter itself rejects a future backend");
        BatteryEditorInput editor{};editor.oem=true;editor.oemChargeWritable=true&&honorBatteryBackend(&unknown);
        check(!batteryEditorAvailability(editor).restart,"unknown charge policy leaves restart unavailable");
        int powerChecks=0;const auto buttons=honorBatteryButtonAvailability(true,false,false,[&]{++powerChecks;return false;});
        check(!buttons.percentage&&std::none_of(buttons.policy.begin(),buttons.policy.end(),[](bool b){return b;})&&powerChecks==0,
            "unknown charge policy disables Honor buttons without power query");
    }
    {
        using namespace mc::windows;std::vector<std::string> calls;FakeHonorBattery battery(calls);int selected=90;
        NativeBatteryActions actions{
            [&](int fallback){calls.push_back("selected:"+std::to_string(fallback));return selected;},
            [&](int value){calls.push_back("save-restart:"+std::to_string(value));},
            [&]{calls.push_back("save-capacity");},
            [&](std::string text){check(text.find("confirmed")!=std::string::npos,"Honor confirmed log");calls.push_back("log");}};
        for(const auto [preset,end]:std::array<std::pair<int,int>,4>{{{0,100},{1,95},{2,85},{3,60}}}){
            selected=end==60?90:80;
            calls.clear();mc::executeBatteryResource(battery,{BatteryWrite::Upper,end},actions);
            const int start=end==60?55:80;
            check(calls==std::vector<std::string>({"read","selected:"+std::to_string(end-5),"write:"+std::to_string(start)+"/"+std::to_string(end),"save-capacity","log"}),
                "Honor upper retains selected start, 60 percent cap, and existing action order");
        }
        calls.clear();bool failed=false;try{mc::executeBatteryResource(battery,{BatteryWrite::Upper,101},actions);}catch(const std::exception&){failed=true;}
        check(failed&&calls.empty(),"invalid Honor upper cannot read preferences or write");
        calls.clear();selected=90;battery.failWrite=true;failed=false;try{mc::executeBatteryResource(battery,{BatteryWrite::Upper,95},actions);}catch(const std::runtime_error&){failed=true;}
        check(failed&&calls==std::vector<std::string>({"read","selected:90","write:90/95"}),"failed Honor upper does not persist or log success");
        battery.failWrite=false;battery.state.chargeWritable=true;battery.state.chargeEnd=95;calls.clear();
        mc::executeBatteryResource(battery,{BatteryWrite::Restart,90},actions);
        check(calls==std::vector<std::string>({"read","write:90/95","save-restart:90"}),"Honor restart reads fresh upper before write and save");
        calls.clear();failed=false;try{mc::executeBatteryResource(battery,{BatteryWrite::Restart,95},actions);}catch(const std::runtime_error&){failed=true;}
        check(failed&&calls==std::vector<std::string>{"read"},"invalid restart does not write or save");
        calls.clear();battery.state.chargeWritable=false;failed=false;
        try{mc::executeBatteryResource(battery,{BatteryWrite::Restart,90},actions);}catch(const std::runtime_error&){failed=true;}
        check(failed&&calls==std::vector<std::string>{"read"},"unavailable Honor charge control does not write");
    }
    {
        using namespace mc::windows;std::vector<std::string> calls;auto gate=std::make_shared<std::recursive_mutex>();
        FakeHonorBattery upper(calls,gate),restart(calls,gate);auto hardware=std::make_shared<mc::LaptopControlSnapshot>();
        hardware->chargeWritable=true;hardware->chargeEnd=95;upper.sharedState=restart.sharedState=hardware;
        std::promise<void> upperEntered,releaseUpper,restartAttempt;auto entered=upperEntered.get_future();
        auto release=releaseUpper.get_future().share();auto attempting=restartAttempt.get_future();
        NativeBatteryActions first{[&](int){calls.push_back("select");upperEntered.set_value();release.wait();return 75;},
            [&](int){calls.push_back("save-restart");},[&]{calls.push_back("save-capacity");}, {}};
        NativeBatteryActions second{[](int){return 80;},[&](int){calls.push_back("save-restart");},[]{}, {}};
        auto a=std::async(std::launch::async,[&]{mc::executeBatteryResource(upper,{BatteryWrite::Upper,85},first);});
        const bool firstReady=entered.wait_for(std::chrono::seconds(3))==std::future_status::ready;
        auto b=std::async(std::launch::async,[&]{restartAttempt.set_value();mc::executeBatteryResource(restart,{BatteryWrite::Restart,80},second);});
        const bool attempted=attempting.wait_for(std::chrono::seconds(3))==std::future_status::ready;
        const bool blocked=b.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout;
        releaseUpper.set_value();a.get();b.get();
        check(firstReady&&attempted&&blocked&&calls==std::vector<std::string>({"read","select","write:75/85","save-capacity","read","write:80/85","save-restart"})&&hardware->chargeEnd==85,
            "Honor restart sees new upper after serialized update and does not restore stale upper");
    }
    {
        const std::wstring name=L"Local\\MagicControl.HwmiGate.Test."+std::to_wstring(GetCurrentProcessId());
        std::promise<void> attempted,acquired;auto attempt=attempted.get_future(),done=acquired.get_future();std::thread contender;
        bool blocked=false;
        {mc::huawei::WmiGate outer(name.c_str());{mc::huawei::WmiGate nested(name.c_str());}
            contender=std::thread([&]{attempted.set_value();mc::huawei::WmiGate other(name.c_str());acquired.set_value();});
            const bool ready=attempt.wait_for(std::chrono::seconds(3))==std::future_status::ready;
            blocked=ready&&done.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout;}
        contender.join();check(blocked&&done.wait_for(std::chrono::seconds(0))==std::future_status::ready,
            "real named gate is reentrant and releases to competing thread");
        try{mc::huawei::WmiGate gate(name.c_str());throw std::runtime_error("test");}catch(const std::runtime_error&){}
        auto retry=std::async(std::launch::async,[&]{mc::huawei::WmiGate afterFailure(name.c_str());return true;});
        check(retry.wait_for(std::chrono::seconds(3))==std::future_status::ready&&retry.get(),
            "exception releases real gate for another thread");
    }
    {
        const auto id=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        const auto base=L"Local\\MagicControl.HwmiGate.ProcessTest."+id;
        HANDLE attempt=CreateEventW(nullptr,TRUE,FALSE,(base+L".attempt").c_str());
        HANDLE acquired=CreateEventW(nullptr,TRUE,FALSE,(base+L".acquired").c_str());
        check(attempt&&acquired,"create isolated cross-process test events");
        std::array<wchar_t,32768> executable{};const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        check(length>0&&length<executable.size(),"locate test executable");
        auto command=L"\""+std::wstring(executable.data(),length)+L"\" --hwmi-gate-child "+id;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
        bool started=false,attempted=false,blocked=false;
        {mc::huawei::WmiGate outer(base.c_str());
            started=CreateProcessW(executable.data(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE;
            if(started){attempted=WaitForSingleObject(attempt,3000)==WAIT_OBJECT_0;
                blocked=attempted&&WaitForSingleObject(acquired,150)==WAIT_TIMEOUT;}}
        bool finished=false;DWORD code=99;
        if(started){const bool entered=WaitForSingleObject(acquired,3000)==WAIT_OBJECT_0;
            finished=WaitForSingleObject(child.hProcess,3000)==WAIT_OBJECT_0&&GetExitCodeProcess(child.hProcess,&code)&&entered;
            CloseHandle(child.hThread);CloseHandle(child.hProcess);}
        CloseHandle(attempt);CloseHandle(acquired);
        check(started&&attempted&&blocked&&finished&&code==0,"named gate serializes separate processes and releases after owner exits scope");
    }
    {
        using namespace mc::windows;
        std::vector<int> calls;bool battery=false,locked=false,accept=true,fieldsValid=true;
        CustomClickContext context{true,false,false,true,false,
            [&]{calls.push_back(1);return true;},[&]{calls.push_back(2);return battery;},[&]{calls.push_back(3);return locked;}};
        CustomEditorActions actions{
            [&]()->std::optional<std::array<int,3>>{calls.push_back(4);if(!fieldsValid)return {};return std::array<int,3>{20,30,40};},
            [&](auto& values){calls.push_back(5);values={11,12,13};},
            [&](auto values){calls.push_back(6);values[0]=5;return values;},
            [&](const auto&){calls.push_back(7);return accept;}};
        const std::array<int,3> initial{10,15,20};
        auto p=prepareCustomEditor(context,initial,actions);
        check(p.decision==CustomClickDecision::OpenGm&&p.values==initial&&calls==std::vector<int>{2},"GM open does not read or write draft");
        calls.clear();context.editing=true;p=prepareCustomEditor(context,initial,actions);
        check(p.decision==CustomClickDecision::ApplyGm&&p.values==std::array<int,3>{20,30,40}&&calls==std::vector<int>({2,4}),"GM apply reads fields after admission");
        calls.clear();battery=true;p=prepareCustomEditor(context,initial,actions);
        check(p.decision==CustomClickDecision::Reject&&calls==std::vector<int>{2},"battery rejection does not read fields");
        battery=false;context.gm=false;context.honor=true;context.editing=false;calls.clear();
        p=prepareCustomEditor(context,initial,actions);
        check(p.decision==CustomClickDecision::OpenHonor&&p.values[0]==5&&calls==std::vector<int>({3,6}),"Honor draft preparation follows gate");
        context.editing=true;accept=false;calls.clear();p=prepareCustomEditor(context,initial,actions);
        check(p.error==CustomPrepareError::HonorRange&&calls==std::vector<int>({3,4,7}),"Honor rejects range after field read");
        fieldsValid=false;calls.clear();p=prepareCustomEditor(context,initial,actions);
        check(p.error==CustomPrepareError::Fields&&calls==std::vector<int>({3,4}),"invalid fields skip Honor range check");
        context.honor=false;context.editing=false;calls.clear();p=prepareCustomEditor(context,initial,actions);
        check(p.decision==CustomClickDecision::OpenEc&&p.values==std::array<int,3>{11,12,13}&&calls==std::vector<int>({3,5}),"EC editor loads saved draft without writes");
        locked=true;calls.clear();p=prepareCustomEditor(context,initial,actions);
        check(p.decision==CustomClickDecision::Reject&&calls==std::vector<int>{3},"fan lock prevents draft load");
    }
    {
        using namespace mc::windows;std::vector<std::string> calls;bool fail=false;
        CustomApplyActions actions{
            [&](const auto& values){calls.push_back("apply");if(fail)throw std::runtime_error("fake failure");
                check(values==std::array<int,3>{10,20,30},"prepared values passed to existing writer");return std::array<int,3>{11,21,31};},
            [&](const auto& values){check(values==std::array<int,3>{10,20,30},"GM expectation uses requested values");calls.push_back("confirm");},
            [&](std::string line){check(line.find("Fast=31 Slow=21 STAPM=11")!=std::string::npos,"EC log uses applied values");calls.push_back("log");}};
        CustomEditorPlan plan{CustomClickDecision::ApplyGm,CustomPrepareError::None,{10,20,30}};
        executeCustomEditorApply(plan,actions);check(calls==std::vector<std::string>({"apply","confirm"}),"GM confirmation follows successful write");
        calls.clear();plan.decision=CustomClickDecision::ApplyHonor;executeCustomEditorApply(plan,actions);
        check(calls==std::vector<std::string>{"apply"},"Honor does not receive GM or EC follow-up");
        calls.clear();plan.decision=CustomClickDecision::ApplyEc;executeCustomEditorApply(plan,actions);
        check(calls==std::vector<std::string>({"apply","log"}),"EC log follows successful write");
        calls.clear();fail=true;bool failed=false;
        try{executeCustomEditorApply(plan,actions);}catch(const std::runtime_error&){failed=true;}
        check(failed&&calls==std::vector<std::string>{"apply"},"writer failure suppresses confirmation and success log");
        calls.clear();plan.error=CustomPrepareError::Fields;failed=false;
        try{executeCustomEditorApply(plan,actions);}catch(const std::invalid_argument&){failed=true;}
        check(failed&&calls.empty(),"invalid prepared request cannot call writer");
        calls.clear();plan.error=CustomPrepareError::None;plan.decision=CustomClickDecision::OpenEc;failed=false;
        try{executeCustomEditorApply(plan,actions);}catch(const std::invalid_argument&){failed=true;}
        check(failed&&calls.empty(),"opening editor cannot call writer");
    }
    {
        using namespace mc::windows;using C=SettingsRememberChoice;
        auto s=settingsActionSemantics(SettingsAction{SettingsActionKind::BatteryUpper,2},true,false);
        check(s.remember==C::Ignore&&s.batteryUpper&&!s.lightingChoice&&!s.quickProfile,"battery command semantics");
        s=settingsActionSemantics(SettingsAction{SettingsActionKind::BatteryVoltage,1},true,false);
        check(s.nativeBatteryMode&&s.remember==C::Ignore&&!s.batteryUpper,"native voltage mode keeps its own persistence");
        s=settingsActionSemantics(SettingsAction{SettingsActionKind::CustomPreset,1},true,false);
        check(s.quickProfile&&s.remember==C::Ignore&&!s.lightingChoice,"custom preset retains its own persistence");
        s=settingsActionSemantics(SettingsAction{SettingsActionKind::LightingColor,4},false,false);
        check(s.remember==C::LightingColor&&s.colorChoice&&s.lightingChoice&&!s.cycleTrigger,"color command semantics");
        s=settingsActionSemantics(SettingsAction::cycleRequest({}),false,false);
        check(s.remember==C::LightingCycle&&s.cycleTrigger&&!s.lightingChoice,"cycle interval command semantics");
        s=settingsActionSemantics(SettingsAction{SettingsActionKind::LightingLevel,3},false,false);
        check(s.remember==C::LightingLevel&&s.rememberIndex==3&&s.lightingChoice,"brightness command retains level index");
        s=settingsActionSemantics(SettingsAction{SettingsActionKind::StockPower,2},true,false);
        check(s.remember==C::Performance&&s.rememberIndex==2&&s.quickProfile,"performance command retains preset index");
        s=settingsActionSemantics(SettingsAction{SettingsActionKind::BatteryVoltage,0},true,true);
        check(!s.nativeBatteryMode&&!s.quickProfile,"passive poll cannot become an interactive quick command");
        for(const auto kind:{SettingsActionKind::EcLogger,SettingsActionKind::BatteryRestart,SettingsActionKind::LightingZone,SettingsActionKind::LightingTimeout})
            check(settingsActionSemantics(SettingsAction{kind},true,false).remember==C::Ignore,"diagnostic and unrecognized commands do not persist through generic path");
    }
    {
        using namespace mc::gui::settings_controls;
        SettingsNoticeContext n{};n.profile="wu-jie-15-xpro";n.brightnessMaximum=4;n.pulse=mc::wujiePulsePalette;
        n.id=kBatteryFirst+2;auto message=settingsNotice(n);
        check(message.key=="battery"&&message.percent==85&&message.title=="Лимит зарядки 85%","battery notice stays unchanged");
        n.id=kApuPolicyOn;message=settingsNotice(n);
        check(message.key=="ec-policy"&&message.title=="EC · 7/04","native EC policy notice stays unchanged");
        n.honor=true;message=settingsNotice(n);
        check(message.title=="Turbo Huawei"&&message.detail=="Включаю…","Honor policy notice remains separate");
        n.honor=false;n.gm=true;n.id=kManualPower;message=settingsNotice(n);
        check(message.key=="performance"&&message.title=="Intel CPU: Custom","native custom notice stays unchanged");
        n.id=kLevelFirst+2;message=settingsNotice(n);
        check(message.key=="keyboard"&&message.percent==50&&message.title=="Клавиатура · 2 / 4","brightness notice stays unchanged");
        n.id=kColorFirst;n.effect=1;n.pulse=mc::unknownGmPulsePalette;message=settingsNotice(n);
        check(message.title=="Клавиатура · Пульсация · Preset 1","numbered pulse notice stays unchanged");
        n.ite=true;n.zone=mc::LightingZone::Logo;n.effect=0;message=settingsNotice(n);
        check(message.key=="lighting-logo"&&message.title=="Логотип · Белый","lighting zone notice remains scoped");
        n.id=999;message=settingsNotice(n);check(message.key=="settings"&&message.icon==mc::gui::osd::Icon::Information,"unknown control notice stays generic");
    }
    {
        using namespace mc::windows;std::vector<int> calls;
        SettingsBackendTestAccess::Factories factories{
            [&]()->std::unique_ptr<mc::LaptopControl>{calls.push_back(1);return {};},
            [&]()->std::shared_ptr<Gm6Controller>{calls.push_back(2);return {};},
            [&]()->std::shared_ptr<NativeBatteryController>{calls.push_back(3);return {};},
            [&]()->std::shared_ptr<NativeBatteryController>{calls.push_back(4);return {};},
            [&]()->std::shared_ptr<IteLightingController>{calls.push_back(5);return {};}};
        static_cast<void>(SettingsBackendTestAccess::acquire(true,false,factories));
        check(calls.empty(),"preview never creates a backend");
        static_cast<void>(SettingsBackendTestAccess::acquire(false,false,factories));
        check(calls==std::vector<int>({1,2,3,4,5}),"backend factory order preserves no-EC behavior");
        calls.clear();static_cast<void>(SettingsBackendTestAccess::acquire(false,true,factories));
        check(calls==std::vector<int>({2,3,4,5}),"existing EC prevents separate laptop backend creation");
        calls.clear();factories.battery=[&]()->std::shared_ptr<NativeBatteryController>{calls.push_back(3);throw std::runtime_error("test");};
        bool failed=false;try{static_cast<void>(SettingsBackendTestAccess::acquire(false,true,factories));}catch(const std::runtime_error&){failed=true;}
        check(failed&&calls==std::vector<int>({2,3}),"factory failure propagates and stops subsequent setup");
    }
    {
        bool failStart=true;std::atomic<int> actions{};
        SettingsDispatch isolated([&](std::function<void()> task)->std::thread{
            if(failStart)throw std::runtime_error("injected thread creation failure");
            return std::thread(std::move(task));
        });
        auto admitted=isolated.admit(false,true,false);check(admitted.decision==D::Start&&admitted.reservation,"startup reservation");
        isolated.begin(false);bool failed=false;
        try{isolated.launch([&]{++actions;},[]{});}catch(const std::runtime_error&){failed=true;}
        check(failed&&!isolated.busy()&&!isolated.reading()&&actions==0,"failed worker creation resets coordinator without action");
        admitted.reservation.reset();check(!mc::deviceOperations().busy(),"caller can release failed startup reservation");
        failStart=false;check(isolated.admit(false,true,true).decision==D::Start,"next poll admitted after failed creation");
        isolated.begin(true);std::atomic<bool> notified{};isolated.launch([&]{++actions;},[&]{notified=true;});
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!notified.load()&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
        check(notified&&isolated.takeCompletion()&&actions==1,"worker runs normally after failed creation");isolated.finish();
        SettingsDispatch emptyThread([](std::function<void()>)->std::thread{return {};});
        emptyThread.begin(true);failed=false;
        try{emptyThread.launch([]{},[]{});}catch(const std::runtime_error&){failed=true;}
        check(failed&&!emptyThread.busy()&&!emptyThread.reading(),"non-started worker cannot leave coordinator busy");
    }
    {
        const auto read=prepareEcLogger(" 0x770 ","invalid",false,{});
        check(read.request&&read.request->address==0x770&&!read.request->writing,"logger read accepts unused invalid value");
        check(prepareEcLogger("0x1000","00",true,{}).error==EcLoggerInputError::InvalidHex&&
              prepareEcLogger("770","100",true,{}).error==EcLoggerInputError::InvalidHex,
              "logger rejects out-of-range address and byte");
        check(prepareEcLogger("770","00",true,{}).error==EcLoggerInputError::ObservationRequired,
              "logger write requires a prior observation");
        mc::EcByteObservation observed{};observed.address=0x771;observed.value=0x45;
        check(prepareEcLogger("770","00",true,observed).error==EcLoggerInputError::ObservationRequired,
              "logger rejects an observation of another address");
        observed.address=0x770;const auto write=prepareEcLogger("0X770","00",true,observed);
        check(write.request&&write.request->writing&&write.request->value==0&&write.request->expected==0x45,
              "logger preserves zero value and expected byte for backend verification");
    }
    check(settingsCompletionConfirmed(true,false)&&!settingsCompletionConfirmed(true,true)&&
          !settingsCompletionConfirmed(false,false)&&!settingsCompletionConfirmed(false,true),
          "superseded or failed SETs operation cannot be reported as confirmed");
    {
        using namespace mc::windows;
        {
            int chosen=1,preset=0;bool intent=false,editing=true;std::array<int,3> values{10,20,30};
            std::optional<int> mode=3,slot=2;std::optional<std::array<int,3>> limits=std::array<int,3>{40,45,50};
            const auto state=[&](bool applied){return CustomCompletionState{chosen,intent,editing,preset,values,mode,slot,limits,applied};};
            check(!completeCustomSelection(state(true),true,false,true,5)&&mode==3&&slot==2&&limits&&editing,
                "passive completion leaves pending interactive selection intact");
            check(completeCustomSelection(state(true),false,false,true,5)&&chosen==5&&intent&&!editing&&preset==2&&
                values==std::array<int,3>{40,45,50}&&!mode&&!slot&&!limits,"confirmed custom preset selects and updates values");
            chosen=1;preset=0;intent=false;editing=true;values={10,20,30};mode=3;slot=2;limits=std::array<int,3>{40,45,50};
            check(completeCustomSelection(state(true),false,false,false,5)&&chosen==1&&!intent&&editing&&preset==0&&
                values==std::array<int,3>{10,20,30}&&!mode&&!slot&&!limits,"failed custom operation leaves prior selection and values");
            mode=3;slot=2;limits=std::array<int,3>{40,45,50};
            check(completeCustomSelection(state(true),false,true,false,5)&&chosen==1&&!intent&&editing&&preset==0&&
                values==std::array<int,3>{10,20,30},"superseded unconfirmed result cannot select a preset");
        }
        check(parseCustomField(L"0",0,140)==0&&parseCustomField(L"140",0,140)==140,
            "GM custom limits accept zero and upper bound");
        check(parseCustomField(L"5",5,255)==5&&parseCustomField(L"255",5,255)==255,
            "generic custom limits include both bounds");
        for(const auto& [profile,expected]:std::array{
            std::pair{std::string_view("mechrevo-gm6ir0c"),std::array{140,165,250}},
            std::pair{std::string_view("mechrevo-gm5hg0a"),std::array{100,100,200}},
            std::pair{std::string_view("machenike-l16"),std::array{145,145,174}},
            std::pair{std::string_view("eluktronics-hydroc16g2"),std::array{255,255,510}}})
            for(int i=0;i<3;++i)check(gmCustomFieldMaximum(profile,i)==expected[i],"native custom field maximum preserves model range");
        try{static_cast<void>(gmCustomFieldMaximum("mechrevo-gm6ir0c",3));check(false,"invalid custom field index");}
        catch(const std::out_of_range&){}
        check(!parseCustomField(L"",5,255)&&!parseCustomField(L"9x",5,255)&&
              !parseCustomField(L"256",5,255)&&!parseCustomField(L"4",5,255),
            "custom apply rejects empty malformed and out of range input");
        {
            std::vector<int> calls;const std::array<std::wstring,3> fields{L"5",L"20",L"30"};
            const auto parsed=parseCustomFields([&](int i){calls.push_back(10+i);return fields[i];},
                [&](int i){calls.push_back(20+i);return 255;},[&]{calls.push_back(30);return 5;});
            check(parsed&&*parsed==std::array<int,3>{5,20,30}&&
                calls==std::vector<int>({10,20,30,11,21,30,12,22,30}),"custom draft parses in original field order");
            calls.clear();const auto invalid=parseCustomFields([&](int i){calls.push_back(10+i);return i==1?std::wstring(L"bad"):fields[i];},
                [&](int i){calls.push_back(20+i);return 255;},[&]{calls.push_back(30);return 5;});
            check(!invalid&&calls==std::vector<int>({10,20,30,11,21,30}),"invalid custom field stops further reads");
            check(std::wstring(customFieldsError("machenike-l16",true,false)).starts_with(L"L16:")&&
                std::wstring(customFieldsError("mechrevo-gm5hg0a",true,false)).starts_with(L"AMD:")&&
                std::wstring(customFieldsError("mechrevo-gm6ir0c",true,false)).starts_with(L"Intel:")&&
                std::wstring(customFieldsError("huawei-hlyl-wxx9",false,true)).starts_with(L"Honor:")&&
                std::wstring(customFieldsError("unknown",false,false)).starts_with(L"Введите"),
                "custom error retains each model's existing text");
        }
        check(clampCustomDraftField(L"999",5,255)==255&&clampCustomDraftField(L"-3",5,255)==5&&
              !clampCustomDraftField(L"",5,255)&&!clampCustomDraftField(L"12x",5,255),
            "custom draft clamps only complete numeric input");
    }
    {
        SettingsSnapshot s;const auto zone=mc::LightingZone::Keyboard;
        const std::array<mc::KeyboardColor,8> previews{{{255,255,255},{255,0,0},{255,80,0},{255,204,0},{0,255,0},{0,255,255},{0,0,255},{180,0,255}}};
        check(!observedLightingBrightness(s,false,false,false,zone),"unsupported EC brightness unknown");
        s.valid=true;s.keyboardSupported=true;s.keyboard=0x80;
        check(observedLightingBrightness(s,false,false,false,zone)==4,"EC brightness readback");
        check(!observedLightingBrightness(s,true,false,false,zone),"preview has no observed brightness");
        s.oem.keyboardLevel=2;
        check(observedLightingBrightness(s,false,false,true,zone)==2,"OEM brightness remains independent");
        s.keyboard=0x82;check(observedLightingBrightnessIndex(s)==0,"EC off flag wins over brightness bits");
        s.breathing=1;
        check(observedLightingEffect(s,false,zone)==1&&observedLightingPalette(s,false,zone,mc::SingleZoneModel::Wujie,"wu-jie-15-xpro",previews)==0,"Wujie pulse palette mapping");
        s.effect=0x80;check(observedLightingEffect(s,false,zone)==2&&observedLightingPalette(s,false,zone,mc::SingleZoneModel::Wujie,"wu-jie-15-xpro",previews)==-1,"rainbow has no selected color");
        s.cycle.active=true;s.cycle.index=4;
        check(observedLightingEffect(s,false,zone)==3&&observedLightingPalette(s,false,zone,mc::SingleZoneModel::Wujie,"wu-jie-15-xpro",previews)==4,"cycle overrides EC effect");
        mc::LightingZoneInfo info;info.zone=zone;info.configured=mc::LightingConfiguration{};
        info.configured->effect=2;info.configured->level=3;info.configured->rgb=previews[6];s.lighting.zones.push_back(info);
        check(observedLightingEffect(s,true,zone)==1&&observedLightingBrightness(s,false,true,false,zone)==3&&
              observedLightingPalette(s,true,zone,mc::SingleZoneModel::Wujie,"unknown",previews)==6,"HID selected zone overrides EC cycle");
        s.lighting.zones.clear();
        check(observedLightingEffect(s,true,zone)==-1&&!observedLightingBrightness(s,false,true,false,zone),"missing HID zone remains unknown");
    }
    {
        SettingsSnapshot clipped;clipped.valid=true;clipped.power.acceptedValid=true;
        clipped.power.accepted={25,25,30};clipped.power.overrides={40,40,45};
        clipped.apu.stapmLimitWatts=40.;clipped.apu.slowLimitWatts=40.;clipped.apu.fastLimitWatts=45.;
        check(observedSettingsPowerLimits(clipped,false,true)==std::array<std::optional<double>,3>{25.,25.,30.},
            "Wujie fields show accepted EC rather than request or SMU");
        check(observedSettingsPowerLimits(clipped,false,false)==std::array<std::optional<double>,3>{40.,40.,45.},
            "other SMU-backed devices keep their original readings");
        clipped.gm6.connected=true;clipped.gm6.limits={10,20,30};
        check(observedSettingsPowerLimits(clipped,true,false)==std::array<std::optional<double>,3>{10.,20.,30.},
            "GM6 native EC limits are unchanged");
        clipped.power.acceptedValid=false;
        check(!observedSettingsPowerLimits(clipped,false,true)[0],"failed EC observation does not substitute SMU");
        clipped.power.acceptedValid=true;clipped.valid=false;
        check(!observedSettingsPowerLimits(clipped,false,true)[0],"invalid snapshot cannot expose stale accepted limits");
        for(unsigned language=1;language<4;++language){
            mc::gui::ui::setLanguage(language);
            const auto text=mc::gui::ui::translate(wujiePowerComparisonText(clipped));
            check(text.find("сверка")==std::string::npos&&text.find("нет свежего")==std::string::npos,
                "EC/SMU comparison is translated in all application languages");
        }
        mc::gui::ui::setLanguage(0);
        SettingsSnapshot snapshot;
        snapshot.keyboard=42;snapshot.effect=128;snapshot.breathing=3;snapshot.color={1,2,3};
        snapshot.gm6.status="quoted \"status\"\nnext";
        const auto unknown=settingsSnapshotAudit(snapshot);
        check(unknown.find("\"keyboard_supported\":false")!=std::string::npos&&
              unknown.find("\"raw_78C\":null")!=std::string::npos&&
              unknown.find("\"raw_767\":null")!=std::string::npos&&
              unknown.find("\"pulse_7C5_low3\":null")!=std::string::npos&&
              unknown.find("\"raw_rgb\":null")!=std::string::npos,
              "unsupported keyboard does not expose stale raw values");
        check(unknown.find("quoted \\\"status\\\"\\u000anext")!=std::string::npos,
              "snapshot audit escapes status text");
        snapshot.valid=true;snapshot.keyboardSupported=true;snapshot.keyboard=0;
        const auto known=settingsSnapshotAudit(snapshot);
        check(known.find("\"valid\":true")!=std::string::npos&&
              known.find("\"raw_78C\":0")!=std::string::npos&&
              known.find("\"raw_767\":128")!=std::string::npos&&
              known.find("\"pulse_7C5_low3\":3")!=std::string::npos&&
              known.find("\"raw_rgb\":[1,2,3]")!=std::string::npos,
              "snapshot audit preserves known raw values including zero");
    }
    {
        using namespace mc::windows;
        SettingsSnapshot s;int calls=0;const auto fallback=[&]{++calls;return std::wstring(L"system fallback");};
        check(batteryPortSummary(s,true,false,false,false,true,fallback)==L"Просмотр профиля"&&calls==0,"preview does not query fallback");
        check(batteryPortSummary(s,false,false,false,false,true,fallback)==L"Порт зарядки: данные EC недоступны.","missing EC charger snapshot");
        s.valid=true;s.chargerRead=true;
        check(batteryPortSummary(s,false,false,false,false,true,fallback)==L"Порт зарядки: внешнее питание не подтверждено EC.","zero connector flags remain unconfirmed");
        s.typecStatus=0x37;s.typecPowerA=65;s.typecPowerB=100;
        check(batteryPortSummary(s,false,false,false,false,true,fallback)==L"Порт зарядки: DC (круглый разъём) + USB-C 1 + USB-C 2 · 100 Вт", "Wujie connector flags and rating");
        s.batteryTelemetry.onlineKnown=true;s.batteryTelemetry.online=true;s.nativeBattery.chargerRaw=0x03;
        s.nativeBattery.typeCPowerA=65;s.nativeBattery.typeCPowerB=100;
        check(batteryPortSummary(s,false,true,true,false,true,fallback)==L"Порт зарядки: DC (круглый разъём) + USB-C (порт не определён) · 100 Вт", "native connector masks take precedence");
        s.nativeBattery.chargerRaw=0;
        check(batteryPortSummary(s,false,true,true,false,true,fallback)==L"Разъём питания: DC (круглый разъём)","GM physical connector fallback preserved");
        s.batteryTelemetry.dynamicStale=true;
        check(batteryPortSummary(s,false,true,true,false,true,fallback)==L"system fallback"&&calls==1,"stale native supply falls back to OS status");
        check(batteryPortSummary(s,false,false,false,true,false,fallback)==L"system fallback"&&calls==2,"Honor uses separate OS status");
    }
    {
        using namespace mc::windows;
        SettingsSnapshot observed;
        check(selectedBatteryThreshold(observed,false,false,"unknown")==-1,"unknown EC battery selection");
        observed.valid=true;observed.batteryPlatform=4;
        for(const auto [raw,expected]:std::array<std::pair<int,int>,6>{{{0,0},{100,0},{95,1},{85,2},{60,3},{10,-1}}}){
            observed.battery=static_cast<std::uint8_t>(raw|0x80);
            check(selectedBatteryThreshold(observed,false,false,"wu-jie-15-xpro")==expected,"EC threshold selection preserves masked raw mapping");
        }
        observed.batteryPlatform=7;observed.batteryRom=255;observed.battery=85;
        check(selectedBatteryThreshold(observed,false,false,"wu-jie-15-xpro")==0,"EC 7/FF highlights unrestricted");
        observed.batteryRom=4;
        check(selectedBatteryThreshold(observed,false,false,"wu-jie-15-xpro")==2&&batteryPolicyActive(observed,false),"EC platform 7 percentage selection and policy");
        observed.batteryPlatform=6;
        check(selectedBatteryThreshold(observed,false,false,"unknown")==-1&&!batteryPolicyActive(observed,false),"unknown platform has no selection");
        observed.oem.chargeEnd=85;observed.battery=85;
        check(selectedBatteryThreshold(observed,false,true,"huawei-hlyl-wxx9")==2,"Honor selection does not borrow platform mapping");
        observed.oem.chargeEnd=0;
        check(selectedBatteryThreshold(observed,false,true,"huawei-hlyl-wxx9")==-1,"Honor missing threshold is unknown");
        observed.nativeBattery.connected=true;observed.nativeBattery.upperRaw=85;
        observed.nativeBattery.enforcementOn=true;
        check(selectedBatteryThreshold(observed,true,false,"mechrevo-gm6ir0c")==2&&batteryPolicyActive(observed,true),"native selection and enforcement readback");
        observed.nativeBattery.platformRaw=7;observed.nativeBattery.romIdRaw=255;
        check(selectedBatteryThreshold(observed,true,false,"mechrevo-gm6ir0c")==0,"native 7/FF unrestricted selection");
        observed.nativeBattery.platformRaw.reset();observed.nativeBattery.romIdRaw.reset();observed.nativeBattery.upperRaw=0;observed.nativeBattery.configuredUpper.reset();
        check(selectedBatteryThreshold(observed,true,false,"mechrevo-gm6ir0c")==-1,"native zero baseline remains unknown");
        observed.nativeBattery.configuredUpper=100;
        check(selectedBatteryThreshold(observed,true,false,"mechrevo-gm6ir0c")==0,"native configured 100 percent selection");
    }
    {
        using namespace mc::windows;
        BatteryEditorInput input{};input.native=true;input.nativePercentageWritable=true;input.hasObservation=true;
        auto editor=batteryEditorAvailability(input);
        check(editor.address&&editor.value&&editor.restart&&editor.restartMaximum==95,"native battery editor availability");
        input.gmAmd=true;editor=batteryEditorAvailability(input);
        check(editor.address&&editor.value&&!editor.restart,"GM AMD restart remains unavailable");
        input.native=false;input.gmAmd=false;input.oem=true;input.oemChargeWritable=true;
        editor=batteryEditorAvailability(input);
        check(!editor.address&&!editor.value&&editor.restart&&editor.restartMaximum==99,"Honor editor stays separate from EC logger");
        input.interactiveBusy=true;editor=batteryEditorAvailability(input);
        check(!editor.address&&!editor.value&&!editor.restart,"interactive gate blocks battery editors");
        input.interactiveBusy=false;input.oem=false;input.ec=true;input.snapshotValid=true;input.busy=true;input.reading=true;
        editor=batteryEditorAvailability(input);check(editor.address&&editor.value&&editor.restart,"passive read retains existing editor policy");
        input.preview=true;editor=batteryEditorAvailability(input);check(!editor.address&&!editor.value&&!editor.restart,"preview cannot enable battery controls");
        for(int flags=0;flags<256;++flags)for(int mode=0;mode<3;++mode){
            NativeVoltageButtonInput v{bool(flags&1),bool(flags&2),bool(flags&4),bool(flags&8),
                bool(flags&16),bool(flags&32),bool(flags&64)};
            const bool gate=bool(flags&128);int calls=0;
            const bool expectedBeforeGate=!v.preview&&(!v.busy||v.reading)&&v.backend&&v.writable&&v.context&&(mode==0||v.appPresent);
            const bool actual=nativeVoltageButtonEnabled(v,mode,[&]{++calls;return gate;});
            check(actual==(expectedBeforeGate&&!gate)&&calls==(expectedBeforeGate?1:0),
                "native voltage buttons retain original conditions and lazy gate");
        }
        auto buttons=gmBatteryButtonAvailability(true,false,true,true);
        check(buttons.percentage&&buttons.policy==std::array<bool,3>{true,true,true},"GM confirmed battery controls available");
        buttons=gmBatteryButtonAvailability(true,true,true,true);
        check(!buttons.percentage&&buttons.policy==std::array<bool,3>{false,false,false},"GM queued command disables controls");
        int reads=0;buttons=honorBatteryButtonAvailability(true,true,true,[&]{return ++reads==2;});
        check(buttons.percentage&&buttons.policy==std::array<bool,3>{true,false,true}&&reads==3,"Honor keeps separate supply observations");
        reads=0;buttons=honorBatteryButtonAvailability(false,true,true,[&]{++reads;return false;});
        check(!buttons.percentage&&!buttons.policy[0]&&reads==0,"busy Honor avoids supply reads");
        EcBatteryButtons ec{};ec.native=true;ec.enforcementWritable=true;buttons=ecBatteryButtonAvailability(ec);
        check(!buttons.percentage&&buttons.policy[0],"native battery policy preserves independent validity rule");
        ec.valid=true;ec.percentageWritable=true;ec.busy=true;ec.reading=true;ec.queued=true;
        buttons=ecBatteryButtonAvailability(ec);check(!buttons.percentage&&!buttons.policy[0],"queued native operation blocks controls");
        ec.native=false;ec.enabled=true;buttons=ecBatteryButtonAvailability(ec);
        check(buttons.percentage&&buttons.policy==std::array<bool,3>{true,true,true},"generic EC follows existing verified enable state");
    }
    {
        using namespace mc::windows;
        int batteryReads=0;bool battery=false;const auto supply=[&]{++batteryReads;return battery;};
        GmPowerButtons gm{true,true,true,false,false,true,true,{true,false,true}};
        auto buttons=gmPowerButtonAvailability(gm,supply);
        check(buttons.modes[0]==true&&buttons.modes[4]==true&&buttons.modes[1]==true&&buttons.modes[2]==false&&
            buttons.modes[3]==true&&buttons.custom&&buttons.presets&&buttons.editors&&!buttons.cancelEditing&&batteryReads==2,
            "GM AC mode availability and two supply checks preserved");
        battery=true;batteryReads=0;buttons=gmPowerButtonAvailability(gm,supply);
        check(buttons.modes[0]==false&&buttons.modes[4]==true&&!buttons.custom&&!buttons.editors&&buttons.cancelEditing&&batteryReads==2,
            "GM battery leaves Auto available and cancels Custom editing");
        gm.doubleEncoding=true;gm.codecSupported=false;batteryReads=0;buttons=gmPowerButtonAvailability(gm,supply);
        check(!buttons.custom&&buttons.modes[0]==false&&batteryReads==0,"unsupported GM codec short-circuits supply reads");
        gm.idle=false;gm.doubleEncoding=false;batteryReads=0;buttons=gmPowerButtonAvailability(gm,supply);
        check(!buttons.custom&&buttons.modes[4]==false&&buttons.modes[1]==false&&batteryReads==0,"busy GM disables buttons");
        HonorPowerButtons honor{true,false,true,true,0x370005};battery=false;batteryReads=0;
        buttons=honorPowerButtonAvailability(honor,supply);
        check(buttons.modes[0]==true&&buttons.modes[1]==true&&buttons.modes[2]==true&&!buttons.modes[3]&&
            !buttons.modes[4]&&buttons.custom&&buttons.editors&&batteryReads==2,"Honor AC uses its native stock and SMU policies");
        honor.tableVersion=0x370006;battery=true;batteryReads=0;buttons=honorPowerButtonAvailability(honor,supply);
        check(buttons.modes[0]==false&&buttons.modes[1]==false&&!buttons.custom&&!buttons.editors&&batteryReads==2,
            "Honor unrecognized SMU and battery refuse existing actions");
        EcPowerButtons ec{true,true,false,true,true,true};buttons=ecPowerButtonAvailability(ec);
        check(buttons.modes[0]==false&&buttons.modes[1]==true&&buttons.modes[4]==true&&!buttons.custom&&buttons.presets&&!buttons.editors,
            "Wujie Default FAN retains stock and Auto while limiting custom editor");
        ec.needsRestore=true;ec.wujie=false;buttons=ecPowerButtonAvailability(ec);
        check(buttons.modes[1]==false&&!buttons.modes[4]&&buttons.presets,"generic recovery and omitted Auto preserve existing conditions");
    }
    {
        SettingsSnapshot snapshot;mc::LightingZoneInfo keyboard;keyboard.zone=mc::LightingZone::Keyboard;
        keyboard.writable=true;keyboard.status="keyboard owner";keyboard.configured=mc::LightingConfiguration{};
        keyboard.configured->level=2;keyboard.configured->effect=2;keyboard.configured->rgb={255,128,0};
        snapshot.lighting.zones.push_back(keyboard);projectSettingsHidZone(snapshot,mc::LightingZone::Keyboard);
        check(snapshot.keyboardSupported&&snapshot.keyboardWritable&&snapshot.keyboardOwnerStatus=="keyboard owner"&&
            snapshot.keyboard==64&&snapshot.effect==0&&snapshot.breathing==1&&snapshot.color==mc::KeyboardColor{50,25,0},
            "HID selected zone preserves level, effect, color and owner");
        snapshot.lighting.zones[0].configured->level=0;snapshot.lighting.zones[0].configured->effect=5;
        snapshot.lighting.zones[0].writable=false;projectSettingsHidZone(snapshot,mc::LightingZone::Keyboard);
        check(snapshot.keyboard==2&&snapshot.effect==0x80&&snapshot.breathing==0&&!snapshot.keyboardWritable,
            "HID off and multi remain distinct and respect read-only owner");
        const auto retained=snapshot.keyboard;projectSettingsHidZone(snapshot,mc::LightingZone::Logo);
        check(!snapshot.keyboardSupported&&!snapshot.keyboardWritable&&snapshot.keyboard==retained,
            "missing HID zone is unavailable without inventing values");
        const auto before=snapshot;collectSettingsHidLighting(snapshot,nullptr,mc::LightingZone::Keyboard);
        check(snapshot==before,"missing HID backend performs no read or state change");
        mc::KeyboardBacklightState sample;sample.supported=true;sample.keyboard=0x40;sample.effect=0x80;
        sample.breathing=3;sample.color={10,20,30};sample.pulseRaw=0x83;
        projectSettingsSingleZone(snapshot,sample,true,"owner available");
        check(snapshot.keyboardSupported&&snapshot.keyboardWritable&&snapshot.keyboard==0x40&&snapshot.effect==0x80&&
            snapshot.breathing==3&&snapshot.color==sample.color&&snapshot.keyboardPulseRaw==0x83&&snapshot.keyboardOwnerStatus=="owner available",
            "single-zone metadata and owner remain attached");
        sample.retainedForDisplay=true;projectSettingsSingleZone(snapshot,sample,true,"retained");
        check(snapshot.keyboardRetained&&!snapshot.keyboardWritable,"retained reading cannot permit writing");
        sample.retainedForDisplay=false;projectSettingsSingleZone(snapshot,sample,false,"occupied");
        check(!snapshot.keyboardWritable&&snapshot.keyboardOwnerStatus=="occupied","foreign owner cannot permit writing");
    }
    {
        for(int flags=0;flags<64;++flags)for(int level=-1;level<=5;++level){
            SettingsSnapshot observed;observed.valid=bool(flags&16);observed.keyboard=0x40;
            std::uint64_t revision=7;std::optional<int> last;int checks=0,saves=0,saved=-1;
            ExternalLightingLevelContext c{observed,revision,last,bool(flags&1),bool(flags&2),bool(flags&4),bool(flags&8),
                [&]{++checks;return bool(flags&32);},[&](int v){++saves;saved=v;},{}};
            const bool accepted=!c.preview&&!c.oem;const int clamped=std::clamp(level,0,4);
            const auto encoded=static_cast<std::uint8_t>((clamped<<5)|(clamped==0?2:0));
            const bool applied=accepted&&observed.valid&&(!c.busy||c.reading);
            const bool changed=observeExternalLightingLevel(c,level);
            check(revision==(accepted?8u:7u)&&last==(accepted?std::optional<int>(clamped):std::nullopt)&&
                checks==(accepted?1:0)&&saves==(accepted&&!(flags&32)?1:0)&&
                (saves==0||saved==clamped)&&observed.keyboard==(applied?encoded:0x40)&&
                changed==(applied&&encoded!=0x40),"external lighting event retains previous ordering and OEM isolation");
        }
        SettingsSnapshot observed;observed.valid=true;std::uint64_t revision=0;std::optional<int> last;std::string logged;
        ExternalLightingLevelContext c{observed,revision,last,false,false,false,false,[]{return false;},
            [](int){throw std::runtime_error("save failed");},[&](std::string text){logged=std::move(text);}};
        check(observeExternalLightingLevel(c,3)&&observed.keyboard==0x60&&last==3&&
            logged=="CONFIG keyboard level save failed: save failed","save failure retains event presentation");
    }
    {
        const mc::LightingConfiguration original{3,2,4,1,{12,34,56}};
        for(int selection=-1;selection<=112;++selection){
            auto expected=original;std::string description;bool valid=true;
            if(selection==1||selection==2||selection==3||selection==5){
                expected.effect=selection;
                description=selection==1?"Моно":selection==2?"Пульсация":selection==3?"Волна":"Мульти";
            }else if(selection>=100&&selection<=104){expected.speed=selection-100;description="Скорость "+std::to_string(selection-99);}
            else if(selection==110||selection==111){expected.direction=selection-110;description=selection==110?"Влево":"Вправо";}
            else valid=false;
            const auto actual=selectHidLightingMenu(original,selection);
            check(bool(actual)==valid,"HID menu rejects unknown selection");
            if(actual)check(actual->configuration==expected&&actual->description==description,"HID menu retains existing mapping and other fields");
        }
    }
    {
        LightingButtonInput input{true,true,true,false,0,4,{1,2,0,0,3,0,4,0}};
        auto state=lightingButtonAvailability(input);
        check(state.effect&&state.native&&state.custom&&!state.cycle&&
            std::all_of(state.colors.begin(),state.colors.end(),[](bool v){return v;}),"single-zone mono choices");
        input.effect=1;input.brightnessMaximum=2;state=lightingButtonAvailability(input);
        check(!state.custom&&!state.cycle&&state.colors==std::array<bool,8>{true,true,false,false,true,false,true,false}&&
            state.levels==std::array<bool,5>{true,true,true,false,false},"pulse palette and brightness range preserved");
        input.effect=2;state=lightingButtonAvailability(input);
        check(!state.custom&&std::none_of(state.colors.begin(),state.colors.end(),[](bool v){return v;}),"multi color choices disabled");
        input.effect=3;state=lightingButtonAvailability(input);check(state.cycle&&!state.custom,"cycle editor availability");
        input.hidPalette=true;input.effect=2;state=lightingButtonAvailability(input);
        check(state.custom&&std::all_of(state.colors.begin(),state.colors.end(),[](bool v){return v;}),"HID retains its color policy");
        input.writable=false;state=lightingButtonAvailability(input);
        check(!state.effect&&!state.native&&!state.custom&&!state.cycle&&
            std::none_of(state.levels.begin(),state.levels.end(),[](bool v){return v;}),"unwritable lighting stays unavailable");
        input.writable=true;input.ready=false;state=lightingButtonAvailability(input);
        check(!state.effect&&!state.native,"busy device stays unavailable");
        // Baseline expressions from the two former SETs branches. Exercise
        // missing capabilities, all effects, HID, and every brightness limit.
        for(int bits=0;bits<16;++bits)for(int effect=-1;effect<=3;++effect)for(int maximum=0;maximum<=4;++maximum){
            input.ready=(bits&1)!=0;input.supported=(bits&2)!=0;input.writable=(bits&4)!=0;
            input.hidPalette=(bits&8)!=0;input.effect=effect;input.brightnessMaximum=maximum;
            const auto current=lightingButtonAvailability(input);const bool light=input.ready&&input.supported&&input.writable;
            check(current.effect==light&&current.native==light&&current.cycle==(light&&effect==3)&&
                current.custom==(light&&effect!=1&&(input.hidPalette||effect==0)),"all lighting control branches retain baseline");
            for(std::size_t i=0;i<current.colors.size();++i)
                check(current.colors[i]==(light&&(input.hidPalette||(effect!=2&&(effect!=1||input.pulsePresets[i]!=0)))),"all color availability retains baseline");
            for(std::size_t i=0;i<current.levels.size();++i)
                check(current.levels[i]==(light&&static_cast<int>(i)<=maximum),"all brightness availability retains baseline");
        }
    }
    {
        std::vector<int> order;mc::CustomPresetLibrary library;library.selected=2;library.active=true;
        const auto selection=mc::windows::initializeCustomSelection([&]{order.push_back(1);},[&]{order.push_back(2);return library;});
        check(order==std::vector<int>{1,2}&&selection.selected==2&&selection.active,"custom selection ensures before loading");
        order.clear();try{mc::windows::initializeCustomSelection([&]{order.push_back(1);throw std::runtime_error("ensure failed");},
            [&]{order.push_back(2);return library;});check(false,"ensure failure must propagate");}catch(const std::runtime_error&){}
        check(order==std::vector<int>{1},"failed ensure does not load selection");
        FakeLaptopPower laptop;std::optional<std::array<int,3>> expected;int saves=0;
        mc::windows::CustomModeContext context;context.laptop=&laptop;context.profileId="test-profile";
        const auto persist=[&](std::string_view profile,bool active){check(profile=="test-profile"&&!active,"correct custom intent persisted");
            check(laptop.calls==std::vector<int>{0},"native command precedes custom persistence");++saves;};
        context.persistStockChoice=persist;
        mc::windows::executePowerResource(context,{mc::windows::PowerWrite::Stock,{},0});
        check(saves==1,"successful stock action persists intent");
        try{mc::windows::executePowerResource(context,{mc::windows::PowerWrite::Stock,{},2});check(false,"unsupported stock action");}
        catch(const std::runtime_error&){}
        check(saves==1,"failed stock action does not persist intent");
    }
    {
        using namespace mc::windows;
        mc::DevicePreferences prefs;prefs[mc::DeviceSetting::ManualStapm]=11;prefs[mc::DeviceSetting::ManualSlow]=22;
        prefs[mc::DeviceSetting::ManualFast]=33;prefs[mc::DeviceSetting::ManualPowerEnabled]=1;
        int recipes=0;auto recipe=[&](int n){++recipes;return std::array<int,3>{n,n+1,n+2};};
        PowerStartupState initial{{25,25,30},-1,false,true};
        auto generic=decodePowerStartup(initial,prefs,true,false,{},recipe);
        check(generic.values==std::array<int,3>{11,22,33}&&generic.chosenMode==4&&generic.editorsInitialized&&recipes==0,
            "generic saved manual power restores without a recipe");
        auto unloaded=decodePowerStartup(initial,prefs,false,false,{},recipe);
        check(unloaded.values==initial.values&&!unloaded.editorsInitialized&&unloaded.chosenMode==4,
            "failed load never claims a completed draft");
        auto wujie=decodePowerStartup(initial,prefs,true,true,{},recipe);
        check(wujie.chosenMode==5&&wujie.savedCustomIntent,"Wujie manual maps to Custom");
        prefs[mc::DeviceSetting::ApuAutoPower]=1;
        wujie=decodePowerStartup(initial,prefs,true,true,{},recipe);
        check(wujie.chosenMode==4&&!wujie.savedCustomIntent,"Wujie saved Auto supersedes Custom intent");
        mc::CustomPreset preset;preset.power={7,8,9};
        auto honor=decodePowerStartup(initial,prefs,true,false,preset,recipe);
        check(honor.values==preset.power&&recipes==0,"Honor explicit preset preserves values");
        preset.powerRecipe=2;honor=decodePowerStartup(initial,prefs,true,false,preset,recipe);
        check(honor.values==std::array<int,3>{2,3,4}&&recipes==1,"Honor recipe uses supplied model resolver");
    }
    {
        mc::KeyboardCycleSettings cycle;std::string error;int saves=0;bool succeeds=true;
        const LightingCycleSaver save=[&](const mc::KeyboardCycleSettings& value,std::string& message){
            ++saves;check(value==cycle,"save receives edited settings");if(!succeeds)message="save failed";return succeeds;};
        check(editLightingCycleInterval(cycle,10,error,save)==LightingCycleEditResult::Unchanged&&saves==0,"unchanged interval does not save");
        check(editLightingCycleInterval(cycle,0,error,save)==LightingCycleEditResult::Invalid&&saves==0,"invalid interval does not save");
        check(editLightingCycleInterval(cycle,12,error,save)==LightingCycleEditResult::Saved&&cycle.intervalSeconds==12&&saves==1,"interval saves once");
        const mc::KeyboardColor color{1,2,3};
        check(editLightingCycleColor(cycle,2,color,error,save)==LightingCycleEditResult::Saved&&cycle.colors[2]==color&&saves==2,"color saves once");
        check(editLightingCycleColor(cycle,6,color,error,save)==LightingCycleEditResult::Invalid&&saves==2,"invalid color index does not save");
        succeeds=false;check(editLightingCycleInterval(cycle,13,error,save)==LightingCycleEditResult::Failed&&
            cycle.intervalSeconds==13&&error=="save failed"&&saves==3,"failed save retains prior in-memory editing behavior");
    }
    std::vector<int> order;int expected=0;
    SettingsWorkSteps steps{[&]{order.push_back(1);return std::uint64_t(order.size());},
        [&]{order.push_back(2);expected=42;},[&]{order.push_back(3);},
        [&]{check(expected==42,"expected state read after action");order.push_back(4);},
        [&]{order.push_back(6);},[&]{order.push_back(7);},
        [&](auto,auto){order.push_back(5);},true};
    executeSettingsWorkSteps(steps);
    check(order==std::vector<int>({1,2,1,3,4,5,6,7}),"work stage order");
    order.clear();steps.observe=[&]{order.push_back(3);throw std::runtime_error("read failure");};
    try{executeSettingsWorkSteps(steps);check(false,"read failure must propagate");}catch(const std::runtime_error&){}
    check(order==std::vector<int>({1,2,1,3}),"failure prevents verify and persistence");
    // Each failure point must prevent all later stages, including persistence.
    const auto runFailure=[&](int failedStage){
        std::vector<int> events;
        const auto stage=[&](int id){events.push_back(id);if(id==failedStage)throw std::runtime_error("injected");};
        SettingsWorkSteps work{[]{return std::uint64_t{1};},[&]{stage(1);},[&]{stage(2);},
            [&]{stage(3);},[&]{stage(5);},[&]{stage(6);},[&](auto,auto){stage(4);},true};
        try{executeSettingsWorkSteps(work);check(false,"failure must propagate");}catch(const std::runtime_error&){}
        check(events.size()==static_cast<std::size_t>(failedStage),"failure halts work pipeline");
        for(int i=0;i<failedStage;++i)check(events[static_cast<std::size_t>(i)]==i+1,"work pipeline stage order");
    };
    for(int stage=1;stage<=6;++stage)runFailure(stage);
    order.clear();steps.observe=[&]{order.push_back(3);};steps.saveChoice=false;
    executeSettingsWorkSteps(steps);
    check(order==std::vector<int>({1,2,1,3,4,5,6}),"passive operation does not persist");
    SettingsSnapshot before,after;bool successFlag=false;std::string lastFailure="old";std::vector<int> phases;
    SettingsWorkSteps realSteps{[]{return std::uint64_t{1};},[&]{expected=71;phases.push_back(1);},
        [&]{phases.push_back(2);},[&]{check(expected==71,"success verifies post-action value");phases.push_back(3);},
        {},[&]{phases.push_back(6);},[&](auto,auto){phases.push_back(4);},true};
    SettingsSuccessContext successWork{std::move(realSteps),before,after,[&]{phases.push_back(5);return SettingsResultContext{};},
        []{return std::wstring(L"confirmed");},successFlag,lastFailure,
        +[](const std::string& s){return std::wstring(s.begin(),s.end());}};
    check(executeSettingsSuccess(successWork)==L"confirmed"&&successFlag&&lastFailure.empty(),"success result");
    check(phases==std::vector<int>({1,2,3,4,5,6}),"success worker order");
    successFlag=false;lastFailure="keep";phases.clear();
    successWork.steps.observe=[&]{phases.push_back(2);throw std::runtime_error("observation");};
    try{executeSettingsSuccess(successWork);check(false,"worker observation failure");}catch(const std::runtime_error&){}
    check(!successFlag&&lastFailure=="keep"&&phases==std::vector<int>({1,2}),"worker failure does not persist or mark success");
    // A failed cycle check or persistence must never publish success.  In
    // particular, do not clear the previous error before persistence finishes.
    successWork.steps.observe=[&]{phases.push_back(2);};
    successWork.classification=[&]()->SettingsResultContext{phases.push_back(5);throw std::runtime_error("classification");};
    phases.clear();
    try{executeSettingsSuccess(successWork);check(false,"classification failure");}catch(const std::runtime_error&){}
    check(!successFlag&&lastFailure=="keep"&&phases==std::vector<int>({1,2,3,4,5}),"classification failure does not persist or publish success");
    successWork.classification=[&]{phases.push_back(5);return SettingsResultContext{};};
    successWork.steps.persist=[&]{phases.push_back(6);throw std::runtime_error("persistence");};
    phases.clear();
    try{executeSettingsSuccess(successWork);check(false,"persistence failure");}catch(const std::runtime_error&){}
    check(!successFlag&&lastFailure=="keep"&&phases==std::vector<int>({1,2,3,4,5,6}),"persistence failure does not publish success");
    successWork.steps.persist=[&]{phases.push_back(6);};
    successWork.initialMessage=[]()->std::wstring{throw std::runtime_error("result message");};
    phases.clear();
    try{executeSettingsSuccess(successWork);check(false,"result construction failure");}catch(const std::runtime_error&){}
    check(!successFlag&&lastFailure=="keep"&&phases==std::vector<int>({1,2,3,4,5,6}),
        "failure after persistence does not publish confirmation or erase error");
    successWork.initialMessage=[]{return std::wstring(L"confirmed");};
    successWork.classification=[&]{phases.push_back(5);SettingsResultContext c;c.hasNativeBattery=true;c.saveChoice=true;c.batteryUpper=true;return c;};
    successWork.toWide=+[](const std::string&)->std::wstring{throw std::runtime_error("result conversion");};
    phases.clear();
    try{executeSettingsSuccess(successWork);check(false,"result conversion failure");}catch(const std::runtime_error&){}
    check(!successFlag&&lastFailure=="keep"&&phases==std::vector<int>({1,2,3,4,5,6}),
        "conversion failure after persistence does not publish confirmation");
    std::vector<int> failureOrder;std::string lastError;std::runtime_error problem("read failed");
    auto wideError=+[](const std::string& text){return std::wstring(text.begin(),text.end());};
    SettingsFailureContext failureContext{false,true,lastError,
        [&](const char*){failureOrder.push_back(1);},[&](const char*){failureOrder.push_back(2);},
        [&](const char*){failureOrder.push_back(4);},[&](std::string){failureOrder.push_back(3);},wideError,
        [&]{failureOrder.push_back(5);return false;}};
    const auto failure=processSettingsFailure(problem,failureContext);
    check(failure.clearActual&&lastError=="read failed"&&failure.message==L"Ошибка: read failed","failure recovery result");
    check(failureOrder==std::vector<int>({1,2,3,4,5}),"failure reporting and recovery order");
    failureOrder.clear();failureContext.reading=true;
    const auto passiveFailure=processSettingsFailure(problem,failureContext);
    check(!passiveFailure.clearActual&&failureOrder==std::vector<int>({1,2}),"repeated passive failure neither logs nor recovers");
    bool clearActual=false;int contexts=0;
    auto factory=[&]{++contexts;return failureContext;};
    check(executeSettingsOperation([]{return std::wstring(L"ok");},factory,clearActual)==L"ok"&&contexts==0&&!clearActual,
        "successful work never constructs a failure context");
    failureContext.reading=false;failureOrder.clear();
    check(executeSettingsOperation([]()->std::wstring{throw std::runtime_error("worker failed");},factory,clearActual)==L"Ошибка: worker failed"&&
        contexts==1&&clearActual&&failureOrder==std::vector<int>({1,2,3,4,5}),
        "worker reports and recovers failure before clearing observation");
    clearActual=false;failureOrder.clear();
    check(executeSettingsOperation([]()->std::wstring{throw 42;},factory,clearActual)==L"Ошибка: unknown SETs operation exception"&&
        contexts==2&&clearActual&&failureOrder==std::vector<int>({1,2,3,4,5}),
        "non-standard worker exception uses the same recovery path");
    {
        std::string originalError;std::vector<int> attempted;
        const auto sink=[&](int id){attempted.push_back(id);throw std::runtime_error("diagnostic failure");};
        SettingsFailureContext broken{false,true,originalError,
            [&](const char*){sink(1);},[&](const char*){sink(2);},[&](const char*){sink(4);},
            [&](std::string){sink(3);},+[](const std::string&)->std::wstring{throw std::runtime_error("conversion failure");},
            [&]()->bool{sink(5);return true;}};
        const auto recovered=processSettingsFailure(problem,broken);
        check(recovered.clearActual&&recovered.message==L"Ошибка SETs: сообщение недоступно."&&originalError=="read failed"&&
              attempted==std::vector<int>({1,2,3,4,5}),"diagnostic failures cannot skip recovery or retain write permissions");
        bool unknown=false;
        const auto fallback=executeSettingsOperation([]()->std::wstring{throw std::runtime_error("operation");},
            []()->SettingsFailureContext{throw std::runtime_error("context failure");},unknown);
        check(unknown&&fallback==L"Ошибка SETs: состояние не подтверждено.","failure context exception clears stale state");
    }
    SettingsSnapshot oldWorker,newWorker;oldWorker.gm6.connected=true;oldWorker.gm6.limits={10,20,30};
    std::optional<std::array<int,3>> expectedWorker;bool workerSuccess=false,workerClear=false;
    std::vector<int> workerEvents;
    mc::MonitoringReader stubReader(nullptr);mc::windows::IntelTelemetryReader stubIntel;
    mc::windows::ApuPowerReader stubApu;std::string stubApuLog;
    SettingsWorkerContext worker{oldWorker,newWorker,[]{return SettingsSnapshotContext{};},expectedWorker,true,false,true,
        []()->std::string_view{return "mechrevo-gm6ir0c";},
        [&]{workerEvents.push_back(1);expectedWorker=std::array<int,3>{10,20,30};},
        [&](const SettingsSnapshot&){workerEvents.push_back(4);},
        [&](std::uint64_t start,std::uint64_t applied){workerEvents.push_back(2);
            // quickProfile prevents optional readers in this synthetic test.
            return SettingsSupplementalContext{oldWorker,false,false,true,true,false,false,-1,-1,start,applied,
                {},nullptr,nullptr,nullptr,nullptr,nullptr,stubReader,stubIntel,stubApu,[]{return false;},{},stubApuLog};},
        [&]{workerEvents.push_back(3);return SettingsResultContext{};},
        []{return std::wstring(L"confirmed");},factory,workerSuccess,workerClear,lastError,wideError};
    check(executeSettingsWorker(worker)==L"confirmed"&&workerSuccess&&!workerClear&&
        workerEvents==std::vector<int>({1,2,3,4}),"worker owns action observe verify and persistence order");
    workerSuccess=false;workerClear=false;workerEvents.clear();failureOrder.clear();
    worker.action=[&]{workerEvents.push_back(1);expectedWorker=std::array<int,3>{1,2,3};};
    check(executeSettingsWorker(worker).starts_with(L"Ошибка:")&&!workerSuccess&&workerClear&&
        workerEvents==std::vector<int>({1}),"failed power readback never persists");
    workerSuccess=false;workerClear=false;workerEvents.clear();failureOrder.clear();
    worker.action=[&]{workerEvents.push_back(1);expectedWorker=std::array<int,3>{10,20,30};};
    worker.supplemental=[&](std::uint64_t,std::uint64_t)->SettingsSupplementalContext{
        workerEvents.push_back(2);throw std::runtime_error("supplemental failed");};
    check(executeSettingsWorker(worker).starts_with(L"Ошибка:")&&!workerSuccess&&workerClear&&
        workerEvents==std::vector<int>({1,2}),"supplemental failure never classifies or persists");
    check(c.admit(true,true,false).decision==D::Preview,"preview");
    check(c.admit(false,false,false).decision==D::NoBackend,"backend");
    {
        FakeLaptopPower laptop;std::optional<std::array<int,3>> expected;
        int lockChecks=0,batteryChecks=0;bool locked=true;
        mc::windows::CustomModeContext context;context.laptop=&laptop;
        context.fanDefaultLocked=[&]{++lockChecks;return locked;};context.onBattery=[&]{++batteryChecks;return false;};
        mc::windows::executePowerResource(context,{mc::windows::PowerWrite::Stock,{},0});
        mc::windows::executePowerResource(context,{mc::windows::PowerWrite::Stock,{},1});
        check(laptop.calls==std::vector<int>({0,1})&&lockChecks==0&&batteryChecks==0&&!expected,
            "native profiles keep their typed indices without generic APU checks");
        try{mc::windows::executePowerResource(context,{mc::windows::PowerWrite::Stock,{},2});check(false,"unsupported native profile");}catch(const std::runtime_error&){}
        check(laptop.calls==std::vector<int>({0,1,2})&&lockChecks==0&&batteryChecks==0,"unsupported Honor profile remains rejected by backend");
        try{mc::windows::executePowerResource(context,{mc::windows::PowerWrite::Economy});check(false,"fan default must block override");}catch(const std::runtime_error&){}
        check(lockChecks==1&&batteryChecks==0,"fan lock checked before power source or EC access");
        locked=false;
        try{mc::windows::executePowerResource(context,{mc::windows::PowerWrite::Economy});check(false,"missing EC must fail");}catch(const std::runtime_error&){}
        check(lockChecks==2&&batteryChecks==1,"missing EC fails after existing source check");
    }
    {
        mc::DeviceOperationGate unrelated;
        auto foreign=unrelated.tryBegin();
        check(bool(foreign)&&c.admit(false,true,false,foreign).decision==D::GateBusy,
            "foreign reservation cannot bypass device admission");
        foreign.reset();
        // A held background gate models a power-transition owner: neither
        // polling nor a new command starts until that owner releases it.
        auto sleepOwner=mc::deviceOperations().tryBegin(mc::DeviceOperationKind::Background);
        check(bool(sleepOwner)&&c.admit(false,true,true).decision==D::PassiveGateBusy&&
            c.admit(false,true,false).decision==D::GateBusy,"power transition blocks concurrent work");
        sleepOwner.reset();
        check(c.admit(false,true,true).decision==D::Start,"poll can start after power owner releases gate");
        auto wakeOwner=mc::deviceOperations().tryBegin();
        auto wakeAdmission=c.admit(false,true,false,wakeOwner);
        check(wakeAdmission.decision==D::Start&&wakeAdmission.reservation==wakeOwner&&mc::deviceOperations().busy(),
            "valid wake reservation remains held across handoff");
        wakeAdmission.reservation.reset();wakeOwner.reset();
        check(!mc::deviceOperations().busy(),"wake reservation releases normally");
    }
    check(c.admit(false,true,true).decision==D::Start,"passive admission");
    c.begin(true);
    std::atomic<bool> release{},started{},notified{};
    c.launch([&]{started=true;while(!release.load())std::this_thread::yield();},[&]{notified=true;});
    while(!started.load())std::this_thread::yield();
    auto cmd=c.admit(false,true,false);
    check(cmd.decision==D::Queue&&bool(cmd.reservation),"queued admission");
    c.enqueue({std::nullopt,L"",L"",{},7,std::move(cmd.reservation),42});
    check(c.hasQueued()&&c.priorityPending(),"priority");
    check(c.admit(false,true,false).decision==D::Busy,"single queued command");
    release=true;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!notified.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    check(notified&&c.takeCompletion(),"completion");
    c.finish();auto queued=c.takeQueued();
    check(queued&&queued->command==42&&queued->token==7&&bool(queued->reservation),"queue preservation");
    auto resumed=c.admit(false,true,false,std::move(queued->reservation));
    check(resumed.decision==D::Start&&bool(resumed.reservation),"reservation handoff");
    resumed.reservation.reset();
    std::atomic<bool> failed{},failureNotified{};
    c.begin(false);
    c.launch([&]{try{throw std::runtime_error("simulated failure");}catch(const std::exception&){failed=true;}},[&]{failureNotified=true;});
    const auto failureDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!failureNotified.load()&&std::chrono::steady_clock::now()<failureDeadline)std::this_thread::yield();
    check(failureNotified&&failed&&c.takeCompletion(),"failed operation completion");
    c.finish();
    std::atomic<bool> unexpectedNotified{};
    c.begin(false);c.launch([]{throw std::runtime_error("unhandled worker failure");},[&]{unexpectedNotified=true;});
    const auto unexpectedDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!unexpectedNotified.load()&&std::chrono::steady_clock::now()<unexpectedDeadline)std::this_thread::yield();
    check(unexpectedNotified&&c.takeCompletion()&&bool(c.unhandledFailure()),"unexpected worker error reaches completion");
    try{std::rethrow_exception(c.unhandledFailure());check(false,"worker exception expected");}
    catch(const std::runtime_error& e){check(std::string(e.what())=="unhandled worker failure","worker preserves original exception");}
    c.finish();c.begin(true);
    std::atomic<bool> retryNotified{};c.launch([]{},[&]{retryNotified=true;});
    check(!c.unhandledFailure(),"next worker launch clears old worker error");
    const auto retryDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!retryNotified.load()&&std::chrono::steady_clock::now()<retryDeadline)std::this_thread::yield();
    check(retryNotified&&c.takeCompletion(),"worker can run after unexpected failure");c.finish();
    check(c.admit(false,true,true).decision==D::Start,"poll after failure");
    check(!shouldRestoreLightingCycle(true,true,true,true,true,true,false),"sleep blocks cycle restore");
    check(!shouldRestoreLightingCycle(true,true,true,true,false,false,false),"resume needs confirmed keyboard");
    check(shouldRestoreLightingCycle(true,true,true,true,true,false,false),"confirmed resume allows cycle restore");
    // A native-profile invalidation while a worker is in flight must not
    // publish its older result or clear newer observed state.
    SettingsSnapshot oldResult;oldResult.valid=true;oldResult.keyboard=0x20;
    bool clearOld=true;std::wstring staleMessage=L"old result";
    check(resolveSettingsObservationRevision(oldResult,clearOld,staleMessage,{4,5,8,9,false,true,{}})&&
        !oldResult.valid&&!clearOld&&staleMessage.empty(),"native profile invalidation rejects old worker result");
    oldResult.valid=true;clearOld=false;staleMessage=L"poll";
    check(!resolveSettingsObservationRevision(oldResult,clearOld,staleMessage,{5,5,8,9,true,true,2})&&
        !oldResult.valid&&staleMessage==L"poll","passive stale poll cannot replace confirmed state");
    oldResult.valid=true;oldResult.keyboard=0x20;
    check(!resolveSettingsObservationRevision(oldResult,clearOld,staleMessage,{5,5,8,9,true,false,2})&&
        oldResult.valid&&oldResult.keyboard==0x40,"stale first poll preserves latest observed keyboard level");
    oldResult.valid=true;oldResult.keyboard=0x20;
    check(!resolveSettingsObservationRevision(oldResult,clearOld,staleMessage,{5,5,8,9,false,true,0})&&
        oldResult.valid&&oldResult.keyboard==0x20,"interactive result is not treated as stale passive poll");
    {
        SettingsSnapshot pending;pending.valid=true;bool clear=false,success=true;std::wstring message=L"confirmed";
        auto resolution=resolveSettingsCompletion(pending,clear,success,message,{4,4,8,8,false,true,{}},false,true);
        check(!resolution.superseded&&!resolution.invalidatePendingLogger&&!resolution.invalidateCurrentLogger&&
            !resolution.markLoggerTouched&&pending.valid&&!clear&&success&&message==L"confirmed",
            "current successful completion keeps confirmed state and logger");
        pending.valid=true;clear=false;success=true;message=L"confirmed";
        resolution=resolveSettingsCompletion(pending,clear,success,message,{4,5,8,8,false,true,{}},false,true);
        check(resolution.superseded&&resolution.invalidatePendingLogger&&!resolution.invalidateCurrentLogger&&
            !resolution.markLoggerTouched&&!pending.valid&&!clear&&!success&&message.empty(),
            "superseded completion discards pending logger and confirmation");
        pending.valid=true;clear=false;success=true;message=L"confirmed";
        resolution=resolveSettingsCompletion(pending,clear,success,message,{4,5,8,8,false,true,{}},true,false);
        check(resolution.superseded&&resolution.invalidatePendingLogger&&resolution.invalidateCurrentLogger&&
            resolution.markLoggerTouched&&!pending.valid&&clear&&!success&&message==L"Ошибка SETs: состояние не подтверждено.",
            "worker exception overrides superseded result and revokes both logger snapshots");
        pending.valid=true;clear=false;success=true;message=L"confirmed";
        resolution=resolveSettingsCompletion(pending,clear,success,message,{4,4,8,8,false,true,{}},true,false);
        check(!resolution.superseded&&resolution.invalidatePendingLogger&&resolution.invalidateCurrentLogger&&
            resolution.markLoggerTouched&&!pending.valid&&clear&&!success,
            "worker exception revokes tentative success even when revision is current");
    }
    {
        SettingsSnapshot current,next;bool loaded=false;
        mc::EcBatteryDisplayCache batteryCache;mc::LaptopDisplayCache oemCache;mc::KeyboardDisplayCache keyboardCache;
        next.valid=true;next.keyboardSupported=true;next.keyboardWritable=true;next.keyboard=0x40;
        next.ecBattery.supported=true;next.ecBattery.coherent=true;next.ecBattery.profileId="fixture";
        next.ecBattery.battery.percent=55;next.oem.connected=true;next.oem.chargeWritable=true;next.oem.chargeEnd=85;
        next.gm6.connected=next.gm6.writable=next.gm6.cpuPresetWritable=true;
        next.nativeBattery.connected=next.nativeBattery.writable=next.nativeBattery.percentageWritable=next.nativeBattery.enforcementWritable=true;
        next.batteryTelemetry.healthPercent=90;
        auto transition=transitionSettingsDisplay(current,next,false,false,true,mc::SingleZoneModel::Wujie,loaded,batteryCache,oemCache,keyboardCache);
        check(transition.changed&&transition.accepted&&transition.acceptedBattery&&loaded&&current.valid&&!next.valid,
            "display transition accepts observation once");
        transition=transitionSettingsDisplay(current,next,true,false,true,mc::SingleZoneModel::Wujie,loaded,batteryCache,oemCache,keyboardCache);
        check(transition.changed&&!transition.accepted&&!transition.acceptedBattery&&!loaded&&!current.valid&&
            !current.keyboardWritable&&current.keyboardRetained&&!current.gm6.connected&&!current.gm6.writable&&!current.gm6.cpuPresetWritable&&
            !current.nativeBattery.connected&&!current.nativeBattery.writable&&!current.nativeBattery.percentageWritable&&!current.nativeBattery.enforcementWritable&&
            current.ecBattery.retainedForDisplay&&current.ecBattery.battery.percent==55&&current.oem.retainedForDisplay&&!current.oem.chargeWritable&&
            current.batteryTelemetry.dynamicStale&&current.batteryTelemetry.healthStale&&current.apu.stale,
            "failed display transition retains history without write eligibility");
        next.valid=true;next.keyboardSupported=false;
        transition=transitionSettingsDisplay(current,next,false,false,true,mc::SingleZoneModel::Wujie,loaded,batteryCache,oemCache,keyboardCache);
        check(transition.accepted&&loaded&&current.keyboardRetained&&!current.keyboardWritable&&current.keyboard==0x40,
            "later missing keyboard observation retains non-writable display cache");
    }
    {
        SettingsSnapshot before,after;bool success=false;int runs=0,encodes=0;std::wstring published;
        SettingsWorkerAuditContext auditContext{before,after,100,0,true,success,
            [&](const std::wstring& value){++encodes;return std::string(value.begin(),value.end());},
            [&](std::wstring value){published=std::move(value);}};
        executeAuditedSettingsWorker(auditContext,[&](const SettingsFailureMarkers& markers){
            ++runs;check(bool(markers.history)&&bool(markers.audit),"failure markers exist during worker");
            after.valid=true;success=true;return std::wstring(L"confirmed");});
        check(published==L"confirmed"&&runs==1&&encodes==1&&after.valid,"audited interactive worker keeps result and completion order");
        auditContext.saveChoice=false;success=false;encodes=0;
        executeAuditedSettingsWorker(auditContext,[&](const SettingsFailureMarkers&){++runs;return std::wstring(L"observed");});
        check(published==L"observed"&&runs==2&&encodes==0,"passive worker does not emit interactive completion");
        bool propagated=false;
        try{executeAuditedSettingsWorker(auditContext,[](const SettingsFailureMarkers&)->std::wstring{throw std::runtime_error("unexpected");});}
        catch(const std::runtime_error&){propagated=true;}
        check(propagated&&published==L"observed","unexpected worker exception does not publish a false completion");
    }
    SettingsSnapshot snapshot;snapshot.valid=true;
    SettingsSnapshotContext context{};
    context.hidObservationOverride=[](SettingsSnapshot&){throw std::runtime_error("simulated read failure");};
    mc::MonitoringReader reader(nullptr);mc::windows::ApuPowerReader power;
    check(!recoverSettingsObservation(snapshot,context,nullptr,reader,power)&&!snapshot.valid,"failure clears unavailable observation");
    // The OEM and HID paths retain ownership of their own saved settings.
    // None of these cases may even request our portable preference path.
    int pathRequests=0,logs=0;
    SettingsRememberContext remember{snapshot,SettingsRememberChoice::Ignore,0,100,false,true,false,false,
        []{return LightingControlContext{};},[]{return false;},[&]{++pathRequests;return std::filesystem::path{};},
        []{return false;},[&](std::string){++logs;}};
    rememberSettingsChoice(remember);
    remember.oem=false;remember.nativePower=true;rememberSettingsChoice(remember);
    remember.nativePower=false;remember.nativeBattery=true;rememberSettingsChoice(remember);
    remember.nativeBattery=false;remember.hidLighting=true;remember.choice=SettingsRememberChoice::LightingLevel;rememberSettingsChoice(remember);
    check(pathRequests==0&&logs==0,"OEM native battery and HID paths do not write shared settings");
    remember.hidLighting=false;remember.choice=SettingsRememberChoice::LightingCycle;
    try{rememberSettingsChoice(remember);check(false,"unsupported cycle must fail");}catch(const std::runtime_error&){}
    check(pathRequests==0&&logs==0,"unsupported cycle is not persisted");
    const auto folder=std::filesystem::path(MAGICCONTROL_TEST_ROOT)/
        ("magiccontrol-settings-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(folder),"create isolated settings test directory");
    const auto settings=folder/"device-settings.ini";
    struct Cleanup {std::filesystem::path file,folder;~Cleanup(){std::error_code ignored;
        std::filesystem::remove(file,ignored);auto tmp=file;tmp+=".tmp";std::filesystem::remove(tmp,ignored);
        std::filesystem::remove(folder,ignored);}} cleanup{settings,folder};
    mc::DevicePreferences seed;seed[mc::DeviceSetting::KeyboardLevel]=3;seed[mc::DeviceSetting::ChargeRestart]=90;std::string settingsError;
    const bool seeded=mc::rememberDevicePreferences(seed,settingsError,settings);
    if(!seeded)std::cerr<<"isolated settings error: "<<settingsError<<'\n';
    check(seeded,"seed isolated settings");
    snapshot.battery=95;snapshot.batteryPlatform=4;snapshot.batteryRom=0xff;
    remember.choice=SettingsRememberChoice::Ignore;remember.preferences=[&]{++pathRequests;return settings;};
    check(mc::rememberDevicePreferences(mc::observedBatteryPreferences(snapshot.battery,snapshot.batteryPlatform,snapshot.batteryRom),settingsError,settings),"save confirmed battery projection");
    mc::DevicePreferences saved;
    check(mc::loadDevicePreferences(settings,saved,settingsError)&&saved[mc::DeviceSetting::ChargeLimit]==95&&
        saved[mc::DeviceSetting::KeyboardLevel]==3,"battery update preserves unrelated saved setting");
    remember.choice=SettingsRememberChoice::Performance;remember.index=2;rememberSettingsChoice(remember);
    saved={};
    check(mc::loadDevicePreferences(settings,saved,settingsError)&&saved[mc::DeviceSetting::ApuMode]==2&&
        saved[mc::DeviceSetting::ChargeLimit]==95&&saved[mc::DeviceSetting::KeyboardLevel]==3,
        "power update preserves battery and lighting settings");
    // Exercise the existing persisted meanings, including the legacy 10%
    // value and all three explicitly supported platform/ROM policies. The
    // temporary settings file is isolated; no controller or EC is opened.
    for(const int limit:{0,10,60,85,95,100})for(const int high:{0,128})
        for(const auto [platform,rom,policy]:{std::array<int,3>{4,255,0},std::array<int,3>{7,4,1},std::array<int,3>{7,255,2}}){
            check(mc::rememberDevicePreferences(mc::observedBatteryPreferences(static_cast<std::uint8_t>(limit|high),platform,rom),settingsError,settings),"save confirmed battery projection");
            saved={};check(mc::loadDevicePreferences(settings,saved,settingsError)&&
                saved[mc::DeviceSetting::ChargeLimit]==(limit?limit:100)&&saved[mc::DeviceSetting::ChargeAuto]==policy&&
                saved[mc::DeviceSetting::BatteryPercentageEnabled]==1&&saved[mc::DeviceSetting::BatteryCardView]==1&&
                saved[mc::DeviceSetting::KeyboardLevel]==3&&saved[mc::DeviceSetting::ChargeRestart]==90&&saved[mc::DeviceSetting::ApuMode]==2,
                "battery raw mask and platform policy round trip preserves other settings");
        }
    check(mc::rememberDevicePreferences(mc::observedBatteryPreferences(85,6,255),settingsError,settings),"save unknown policy projection");saved={};
    check(mc::loadDevicePreferences(settings,saved,settingsError)&&saved[mc::DeviceSetting::ChargeAuto]==2,
        "unknown platform does not overwrite the last explicit saved policy");
    for(const auto [raw,preset]:{std::array<int,2>{0x80,0},std::array<int,2>{0x00,1},std::array<int,2>{0x10,2}}){
        mc::windows::rememberObservedPower(settings,true,4,true,raw,mc::EcPowerLimits{20,25,30});saved={};
        check(mc::loadDevicePreferences(settings,saved,settingsError)&&saved[mc::DeviceSetting::ManualPowerEnabled]==1&&
            saved[mc::DeviceSetting::ApuMode]==4&&saved[mc::DeviceSetting::ApuAutoPower]==0&&
            saved[mc::DeviceSetting::Performance]==preset&&saved[mc::DeviceSetting::ManualStapm]==20&&
            saved[mc::DeviceSetting::ManualSlow]==25&&saved[mc::DeviceSetting::ManualFast]==30&&
            saved[mc::DeviceSetting::ChargeLimit]==85&&saved[mc::DeviceSetting::ChargeRestart]==90&&saved[mc::DeviceSetting::KeyboardLevel]==3,
            "manual power round trip preserves battery and lighting");
    }
    mc::windows::rememberObservedPower(settings,true,4,false,0,{});saved={};
    check(mc::loadDevicePreferences(settings,saved,settingsError)&&saved[mc::DeviceSetting::ApuAutoPower]==1&&
        saved[mc::DeviceSetting::ManualPowerEnabled]==0&&saved[mc::DeviceSetting::ChargeLimit]==85,
        "legacy auto power selection round trip preserves battery");
    std::cout<<"settings queue checks passed\n";
}
