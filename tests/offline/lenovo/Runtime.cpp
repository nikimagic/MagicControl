#include "src/platform/windows/SettingsActionWindows.h"
#include "src/platform/windows/NativeFixedFans.h"
#include "src/platform/windows/FanDeviceActionsWindows.h"
#include "src/platform/windows/ConfigurationRestoreWindows.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/PortableSettings.h"
#include "tests/TestBoardFixture.h"
#include "src/platform/common/DiagnosticDumpInternal.h"
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
using namespace mc;using namespace mc::windows;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
const PowerOptions options{{"gpu.offset",55},{"gpu.ctgp",115},{"gpu.boost",15},{"cpu.tau",56},{"cpu.temperature",95},{"gpu.temperature",87}};
struct Cpu final:LaptopControl {
    bool fail{},active{};int writes{};PowerOptions applied;std::array<int,3> values{};
    std::optional<CpuPowerChoice> choice;std::function<void()> confirmed;
    std::string_view customPowerPolicyId()const noexcept override{return "lenovo-legion-kwcn-wmi";}
    std::array<int,3> customCpuPowerMaximum()const override{return {110,140,190};}
    std::array<int,3> customCpuPowerMinimum()const override{return {20,30,55};}
    std::array<int,3> customCpuPowerRecipe(int slot)const override{return {25+20*slot,55+20*slot,65+40*slot};}
    PowerOptions customPowerOptionRecipe(int)const override{return options;}
    void validateCustomPower(const std::array<int,3>& v,const PowerOptions& o)const override{
        const auto lo=customCpuPowerMinimum(),hi=customCpuPowerMaximum();for(int i=0;i<3;++i)if(v[i]<lo[i]||v[i]>hi[i])throw std::invalid_argument("fixture WMI range");
        if(o!=options)throw std::invalid_argument("fixture requires all six declared options");
    }
    std::array<int,3> setCustomPower(const std::array<int,3>& v,const PowerOptions& o)override{
        validateCustomPower(v,o);if(fail)throw std::runtime_error("fixture hardware refused readback");++writes;values=v;applied=o;active=true;if(confirmed)confirmed();return v;
    }
    LaptopControlSnapshot read()override{LaptopControlSnapshot s;s.connected=true;s.customCpuPowerActive=active;return s;}
    std::optional<CpuPowerChoice> savedCpuPowerChoice()const override{return choice;}
    void setStockCpuProfile(int)override{active=false;}
    void setChargeThresholds(int,int)override{throw std::logic_error("battery writer reached");}
    void setFnLock(bool)override{throw std::logic_error("KEY writer reached");}
};
struct Fans final:NativeFixedFans {
    NativeFixedFanState state;int writes{},maximumWrites{},defaults{};bool allowAutomatic{};
    Fans(){state.sharedCurve=true;state.targetRpm=true;state.writable=true;state.mode=0;state.curves.emplace();state.editablePointCounts={10,10};
        for(auto& c:state.curves->draft)for(auto& p:c)p={255,100};
        for(int i=0;i<10;++i)state.curves->draft[0][i]=state.curves->draft[1][i]={20+8*i,5+10*i};}
    NativeFixedFanState read()override{return state;}
    void setCurves(const NativeFanCurves& c)override{check(c[0]==c[1],"independent curve reached native writer");++writes;state.curves->draft=c;state.curves->appliedCurve=true;state.manual=true;state.mode=1;}
    void setFixed(const std::array<int,2>&)override{throw std::logic_error("unexpected fixed request");}
    void setMaximum()override{++maximumWrites;state.manual=true;state.mode=2;state.targetRamped=true;state.curves->appliedCurve=false;}
    NativeFanAutoResult setAutomatic()override{if(!allowAutomatic)throw std::logic_error("power operation reset FANs");++defaults;state.manual=false;state.mode=0;state.curves->appliedCurve=false;return NativeFanAutoResult::Applied;}
};
std::string bytes(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(){try{
    diagnostics::EcDump ram;ram.base=0xc000;unsigned blocks{};
    diagnostics::captureEcBlocks(ram,[&](auto address,auto count,auto*){check(address==0xc000+blocks*16,"RAM dump lost its nonzero base");++blocks;return std::vector<std::uint8_t>(count,0x55);},{},8192);
    check(ram.complete&&ram.bytes.size()==8192&&blocks==512&&ram.readAttempts.back().address==0xdff0,"RAM dump truncated/relocated the8KiB window");
    std::stop_source cancelled;cancelled.request_stop();diagnostics::EcDump partial;partial.base=0xc000;
    diagnostics::captureEcBlocks(partial,[](auto,auto,auto*)->std::vector<std::uint8_t>{throw std::logic_error("cancelled dump reached IO");},cancelled.get_token(),8192);
    check(!partial.complete&&partial.bytes.size()==8192,"cancelled dump claimed completeness");
    const auto presetPath=portableSettingsPath(customPresetFilename("lenovo-legion-kwcn"));
    const auto fanPath=portableSettingsPath(std::string(gmFanCurveFile("lenovo-legion-kwcn")));
    check(!std::filesystem::exists(presetPath)&&!std::filesystem::exists(fanPath),"fixture directory contains prior settings");
    BoardDescriptor board;board.manufacturer="LENOVO";board.baseBoard="LNVNB161216";board.productName="82WK";tests::setTestBoard(board);
    Cpu cpu;auto fans=std::make_shared<Fans>();CustomModeContext context;context.profileId="lenovo-legion-kwcn";context.laptop=&cpu;context.nativeFans=fans;
    const auto draft=fans->state.curves->draft;
    for(int slot=0;slot<3;++slot){executePowerResource(context,{PowerWrite::Preset,{},slot});
        const auto saved=loadCustomPresetLibrary(context.profileId);
        check(saved.active&&saved.selected==slot&&saved.slots[slot]->powerOptions==options,"shared preset lost GPU/Tau/temperature settings");
        check(fans->state.curves->draft==draft&&cpu.applied==options,"preset changed the shared curve or missed extra limits");}
    check(bytes(presetPath).starts_with("MagicControlCustomPresets 5 "),"extended preset format missing");
    const auto before=bytes(presetPath);cpu.fail=true;bool refused{};
    try{executePowerResource(context,{PowerWrite::Preset,{},0});}catch(const std::runtime_error&){refused=true;}
    check(refused&&bytes(presetPath)==before,"failed power command persisted a successful selection");cpu.fail=false;
    SettingsAction action{SettingsActionKind::CustomEditor,1};action.customPlan=CustomEditorPlan{CustomClickDecision::ApplyNative,CustomPrepareError::None,{100,80,150}};action.powerOptions=options;
    SettingsActionServices services;services.custom=[&]{return context;};executeSettingsAction(action,services);
    check(cpu.values==std::array<int,3>{100,80,150}&&cpu.applied==options,"GUI options bypassed/lost at the shared power entry");
    check(loadCustomPresetLibrary(context.profileId).slots[1]->powerOptions==options,"GUI edit lost optional settings on save");
    std::string loadError;FanCurveProfiles saved;check(loadFanCurveProfiles(fanPath,saved,loadError),"shared FAN intent not saved");
    check(saved.linked&&saved.activePointCount(0)==10&&saved.activePointCount(1)==10,"shared FAN count/link persistence lost");
    std::mutex gate;std::condition_variable changed;int restored{};
    const auto factory=[&]()->std::unique_ptr<LaptopControl>{auto c=std::make_unique<Cpu>();c->choice=CpuPowerChoice{0,true,{100,80,150},options};
        c->confirmed=[&]{std::lock_guard lock(gate);++restored;changed.notify_all();};return c;};
    for(const auto reason:{"startup","power_resume"}){
        fans->state.mode=0;fans->state.manual=false;fans->state.curves->appliedCurve=false;
        ConfigurationRestore restore("lenovo-legion-kwcn",factory,[&]{return fans;});if(std::string_view(reason)=="power_resume")restore.suspend();
        const int wanted=restored+1;restore.request(reason);std::unique_lock lock(gate);
        check(changed.wait_for(lock,std::chrono::seconds(8),[&]{return restored>=wanted;}),"startup/resume lost the shared Custom settings");
        check(fans->state.mode==1&&fans->state.curves->draft[0]==fans->state.curves->draft[1],"startup/resume lost Manual CPU=GPU");
    }
    // Hold the shared resource while a fake controller enters restore. A sleep
    // invalidates that request; only the new post-wake request may apply it.
    {
        auto lease=deviceOperations().tryBegin();check(bool(lease),"fixture could not occupy resource gate");
        std::mutex enteredMutex;std::condition_variable enteredChanged;int entered{};
        const auto blockedFactory=[&]()->std::unique_ptr<LaptopControl>{
            {std::lock_guard lock(enteredMutex);++entered;}enteredChanged.notify_all();return factory();};
        fans->state.mode=0;fans->state.manual=false;fans->state.curves->appliedCurve=false;
        const int beforeRestore=restored;const int beforeWrites=fans->writes;
        ConfigurationRestore restore("lenovo-legion-kwcn",blockedFactory,[&]{return fans;});restore.request("startup");
        {std::unique_lock lock(enteredMutex);check(enteredChanged.wait_for(lock,std::chrono::seconds(8),[&]{return entered>=1;}),"fixture restore did not enter admission");}
        restore.suspend();restore.request("power_resume");lease.reset();
        {std::unique_lock lock(gate);
            check(changed.wait_for(lock,std::chrono::seconds(8),[&]{return restored>=beforeRestore+1;}),"post-wake restore did not apply");
            check(!changed.wait_for(lock,std::chrono::milliseconds(1200),[&]{return restored>beforeRestore+1;}),"stale pre-sleep restore also applied");}
        check(fans->writes==beforeWrites+1&&fans->state.mode==1,"post-wake Manual was lost or applied twice");
    }
    const auto presetsBeforeMax=bytes(presetPath);const auto curvesBeforeMax=saved.curves;const int curveWrites=fans->writes;
    FanWriteRequest maximum{FanResourceOperation::Maximum};maximum.profiles=saved;maximum.profiles->curves[0][0][0]={99,99};
    executeFanResource({nullptr,fans.get(),nullptr,{},true,"lenovo-legion-kwcn"},maximum).requireAccepted();
    FanCurveProfiles maxSaved;check(loadFanCurveProfiles(fanPath,maxSaved,loadError),"Max selection not saved");
    check(maxSaved.selected[0]==FanCurvePreset::Max&&maxSaved.curves==curvesBeforeMax&&bytes(presetPath)==presetsBeforeMax&&fans->writes==curveWrites,"Max changed Manual settings, presets or curve commands");
    for(const auto reason:{"startup","power_resume"}){
        fans->state.mode=0;fans->state.manual=false;const int maximumBefore=fans->maximumWrites;
        ConfigurationRestore restore("lenovo-legion-kwcn",factory,[&]{return fans;});if(std::string_view(reason)=="power_resume")restore.suspend();
        const int wanted=restored+1;restore.request(reason);std::unique_lock lock(gate);
        check(changed.wait_for(lock,std::chrono::seconds(8),[&]{return restored>=wanted;}),"Max startup/resume lost power restore");
        check(fans->state.mode==2&&fans->maximumWrites==maximumBefore+1&&fans->writes==curveWrites,"Max startup/resume replayed a Manual curve");
    }
    // The real common Max sampler, but fake resource callbacks and immediate
    // cancellation: no 30s wait, OS backend, driver or physical fan experiment.
    fans->allowAutomatic=true;FanWriteRequest automatic{FanResourceOperation::Default};automatic.origin=FanWriteOrigin::Restore;
    maximum.origin=FanWriteOrigin::Restore;
    const auto entry=[&](const FanWriteRequest& request){executeFanResource({nullptr,fans.get(),nullptr,{},true,"lenovo-legion-kwcn"},request).requireAccepted();};
    entry(automatic);std::stop_source stop;
    auto sample=[]{FanMaximumSample value;value.rpm={1700,1700};return value;};
    const auto probe=measureMaximumFans("fake native flag",[&]{entry(maximum);stop.request_stop();},sample,[&]{entry(automatic);},stop.get_token());
    check(probe.attempted&&probe.commandApplied&&probe.restored&&fans->state.mode==0&&fans->writes==curveWrites,"cancelled dump Max lost native flag cleanup or touched Manual");
    int sampleCount{};const auto failedProbe=measureMaximumFans("fake native flag",[&]{entry(maximum);},[&]{if(sampleCount++)throw std::runtime_error("sensor failed");return sample();},[&]{entry(automatic);},{});
    check(failedProbe.attempted&&failedProbe.commandApplied&&failedProbe.restored&&fans->state.mode==0,"failed dump sensor read left Full Speed active");
    entry(maximum);check(fans->state.mode==2&&fans->writes==curveWrites&&bytes(presetPath)==presetsBeforeMax,"pre-dump Max restore changed Manual/presets");
    FanCurveProfiles common;const auto commonPath=portableSettingsPath("fan-curves.csv");
    check(loadFanCurveProfiles(commonPath,common,loadError),"common Manual fixture not available");
    std::filesystem::remove(fanPath);maximum.origin=FanWriteOrigin::Explicit;entry(maximum);
    FanCurveProfiles firstMax;check(loadFanCurveProfiles(fanPath,firstMax,loadError)&&firstMax.curves==common.curves&&firstMax.activePointCount(0)==10,"first Max replaced legacy/common Manual cells");
    std::cout<<"Lenovo: shared entry, all presets, native Max, unchanged Manual storage, startup/resume, cancelled/failed dump cleanup passed; fake backends only\n";
    std::filesystem::remove(presetPath);std::filesystem::remove(fanPath);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
