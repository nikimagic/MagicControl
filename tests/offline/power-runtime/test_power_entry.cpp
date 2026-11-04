#include "../../../src/platform/windows/SettingsActionWindows.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/PortableSettings.h"
#include "../../../src/platform/windows/SettingsPowerSelectionWindows.h"
#include "../../../src/platform/windows/NativeFixedFans.h"
#include "../../../src/protocols/minisforum/HpbsdFans.h"
#include "../../../src/protocols/minisforum/HpbsdPower.h"
#include "../../../src/devices/minisforum/hpbsd/PowerControlWindows.h"
#include "../../../src/platform/windows/ConfigurationRestoreWindows.h"
#include "../../TestBoardFixture.h"
#include <condition_variable>
#include <chrono>
#include <sstream>
#include <fstream>
#include <iostream>
#include <vector>
using namespace mc::windows;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct FakeCpu final:mc::LaptopControl {
    std::vector<int> calls;std::vector<std::array<int,3>> customCalls;bool battery{},mismatch{},nativeCustom{};
    std::optional<mc::CpuPowerChoice> choice;std::function<void()> applied;
    std::optional<mc::CpuPowerChoice> savedCpuPowerChoice()const override{return choice;}
    std::string_view customPowerPolicyId()const noexcept override{return nativeCustom?"minisforum-hpbsd-windows-smu":"";}
    std::array<int,3> customCpuPowerMaximum()const override{return {255,255,255};}
    std::array<int,3> customCpuPowerRecipe(int slot)const override{return mc::hpbsd::customPowerRecipes.at(slot);}
    std::array<int,3> setCustomCpuPower(const std::array<int,3>& values)override{
        static_cast<void>(mc::hpbsd::customPowerMilliwatts(values));if(battery||mismatch)throw std::runtime_error("native Custom not confirmed");customCalls.push_back(values);if(applied)applied();return values;
    }
    mc::LaptopControlSnapshot read()override{return {};}
    void setChargeThresholds(int,int)override{throw std::runtime_error("unexpected charge operation");}
    void setFnLock(bool)override{throw std::runtime_error("unexpected key operation");}
    void setStockCpuProfile(int profile)override{
        calls.push_back(profile);
        if(profile>1||battery||mismatch)throw std::runtime_error("native backend did not confirm the profile");
    }
};
struct FakeFans final:NativeFixedFans {
    NativeFixedFanState state;int curveWrites{},defaults{};
    FakeFans(){state.writable=true;state.curves.emplace();state.curves->draft=mc::hpbsd::defaultDraft(0xb1);state.mode=0;}
    NativeFixedFanState read()override{return state;}
    void setFixed(const std::array<int,2>&)override{throw std::runtime_error("unexpected fixed target");}
    NativeFanAutoResult setAutomatic()override{++defaults;throw std::runtime_error("unexpected Default");}
    void setCurves(const mc::NativeFanCurves& value)override{++curveWrites;state.manual=true;state.mode=1;state.curves->draft=value;state.curves->appliedCurve=true;}
};
}
int main(){try{
    mc::FanCurveProfiles points;check(points.activePointCount(0)==16&&points.activePointCount(1)==16,"all devices must default16/16");
    const auto untouched=points.curves;points.setActivePointCount(0,8);points.setActivePointCount(1,3);check(points.curves==untouched,"changing active count rewrote curve values");
    bool badCount{};try{points.setActivePointCount(1,17);}catch(const std::out_of_range&){badCount=true;}check(badCount,"invalid point count accepted");
    const auto absentLibrary=mc::settingsReadPath(mc::portableSettingsPath(mc::customPresetFilename("wu-jie-15-xpro")));
    check(!std::filesystem::exists(absentLibrary),"fixture executable must have no personal settings");
    mc::setCustomPresetActive("wu-jie-15-xpro",false);
    check(!std::filesystem::exists(absentLibrary),"clearing an absent Custom selection created a library");
    FakeCpu backend;int persisted{};CustomModeContext context;
    context.profileId="fixture";context.laptop=&backend;
    context.onBattery=[]()->bool{throw std::runtime_error("generic supply policy reached a native backend");};
    context.fanDefaultLocked=[]()->bool{throw std::runtime_error("generic FAN policy reached a native backend");};
    context.persistStockChoice=[&](std::string_view profile,bool active){
        check(profile=="fixture"&&!active&&!backend.calls.empty(),"selection saved before native confirmation");++persisted;
    };
    SettingsActionServices services;services.custom=[&]{return context;};
    executeSettingsAction({SettingsActionKind::StockPower,1},services); // GUI input is one-based.
    executePowerResource(context,{PowerWrite::Stock,{},1}); // CLI already decoded to native index.
    executePowerResource(context,{PowerWrite::Stock,{},0,PowerWriteOrigin::Restore});
    check(backend.calls==std::vector<int>({0,1,0})&&persisted==2,"GUI/CLI/restore must share native mapping; restore must not save a new choice");
    for(int failure=0;failure<3;++failure){
        backend.battery=failure==0;backend.mismatch=failure==1;
        bool refused{};try{executePowerResource(context,{PowerWrite::Stock,{},failure==2?2:1});}catch(const std::runtime_error&){refused=true;}
        check(refused&&persisted==2,"supply change, readback failure or unsupported mode persisted success");
    }
    // Every catalogued model keeps its own admission. A name/capability alone
    // cannot turn a missing transport into a writable CPU backend.
    int models{};
    for(const auto* profile:mc::deviceProfiles()){
        CustomModeContext absent;absent.profileId=std::string(profile->id);++models;
        absent.persistStockChoice=[](std::string_view,bool){throw std::logic_error("missing backend persisted a selection");};
        bool refused{};try{executePowerResource(absent,{PowerWrite::Stock,{},0});}catch(const std::runtime_error&){refused=true;}
        check(refused,"catalog declaration granted CPU write access");
        for(const int slot:{-1,3})for(const auto kind:{PowerWrite::Preset,PowerWrite::CustomEditor}){
            bool badSlot{};try{executePowerResource(absent,{kind,{10,15,20},slot});}catch(const std::invalid_argument&){badSlot=true;}
            check(badSlot,"invalid slot must fail before file/backend access");
        }
    }
    CustomModeContext absent;int fanChecks{},supplyChecks{};bool locked=true,battery=false;
    absent.fanDefaultLocked=[&]{++fanChecks;return locked;};absent.onBattery=[&]{++supplyChecks;return battery;};
    for(int stage=0;stage<3;++stage){locked=stage==0;battery=stage==2;bool refused{};
        try{executePowerResource(absent,{PowerWrite::Economy});}catch(const std::runtime_error&){refused=true;}
        check(refused,"minimal must not invent a backend");
    }
    check(fanChecks==3&&supplyChecks==2,"minimal admission order changed");
    // The same resource entry admits the new typed policy without any Honor
    // recipe, Wujie EC, Default reset or hardware object in this fixture.
    backend.nativeCustom=true;backend.battery=backend.mismatch=false;
    CustomModeContext native;native.profileId="minisforum-hpbsd";native.laptop=&backend;native.nativeFans=std::make_shared<FakeFans>();
    const auto fans=std::static_pointer_cast<FakeFans>(native.nativeFans);
    for(const auto origin:{PowerWriteOrigin::Explicit,PowerWriteOrigin::Restore})executePowerResource(native,{PowerWrite::Custom,{5,70,255},0,origin});
    check(backend.customCalls.size()==2&&fans->curveWrites==0&&fans->defaults==0,"CPU-only Custom changed FANs or bypassed typed writer");
    bool rejected{};try{executePowerResource(native,{PowerWrite::Custom,{30,25,40}});}catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&backend.customCalls.size()==2,"invalid Custom order reached a writer");
    const auto path=mc::portableSettingsPath(mc::customPresetFilename(native.profileId));
    check(!std::filesystem::exists(path),"fixture has an unexpected preset library");
    {
        std::ostringstream text;text<<"MagicControlCustomPresets 3 minisforum-hpbsd 0 1\n";
        const auto old=mc::hpbsd::defaultDraft(0xb1);
        for(int slot=0;slot<3;++slot){text<<slot+1<<" 1\n20 25 30 0 0\n";for(int i=0;i<32;++i)text<<"0 0 0\n";text<<(slot==0?1:0)<<'\n';if(slot==0)for(const auto& row:old)for(int i=0;i<8;++i)text<<row[i].temperature<<' '<<row[i].duty<<'\n';}
        {std::ofstream legacy(path);legacy<<text.str();}
        auto migrated=mc::loadCustomPresetLibrary(native.profileId);
        check(migrated.slots[0]->curvesReady&&!migrated.slots[1]->curvesReady,"legacy v3 curve provenance lost");
        for(int ch=0;ch<2;++ch)for(int i=0;i<mc::hpbsd::bandCount[ch];++i)check(migrated.slots[0]->curves[ch][i].up==old[ch][i].temperature&&migrated.slots[0]->curves[ch][i].duty==old[ch][i].duty,"legacy point changed during canonical import");
        mc::saveCustomPresetLibrary(native.profileId,migrated);const auto again=mc::loadCustomPresetLibrary(native.profileId);
        check(again.slots[0]->curves==migrated.slots[0]->curves,"16-cell canonical serialization changed inactive values");
        std::filesystem::remove(path);
    }
    mc::CustomPresetLibrary library;for(auto& p:library.slots){p.emplace();p->powerRecipe=1;p->curvesReady=false;}
    mc::saveCustomPresetLibrary(native.profileId,library); // Existingv2 stays readable.
    check(!mc::loadCustomPresetLibrary(native.profileId).slots[0]->curvesReady,"legacy EC curve was reinterpreted as native bands");
    const auto original=fans->state.curves->draft;
    executePowerResource(native,{PowerWrite::Preset,{},0});
    auto saved=mc::loadCustomPresetLibrary(native.profileId);
    check(saved.active&&saved.selected==0&&saved.slots[0]->power==std::array<int,3>{20,25,30}&&saved.slots[0]->curvesReady&&saved.slots[0]->curves[0][0].up==original[0][0].temperature,"native preset power/bands did not round-trip through the common library");
    check(fans->curveWrites==1&&fans->defaults==0,"native preset did not use the common Manual operation");
    auto second=original;second[0][0].temperature=20;second[0][0].duty=5;
    saved.slots[1]->powerRecipe=0;saved.slots[1]->power={65,70,100};saved.slots[1]->curvesReady=true;for(int ch=0;ch<2;++ch)for(int i=0;i<16;++i)saved.slots[1]->curves[ch][i]={second[ch][i].temperature,i?second[ch][i-1].temperature:0,second[ch][i].duty};mc::saveCustomPresetLibrary(native.profileId,saved);
    executePowerResource(native,{PowerWrite::Preset,{},1});
    check(fans->state.curves->draft==second&&backend.customCalls.back()==std::array<int,3>{65,70,100},"preset changed point count/data or imported another model's limits");
    const auto beforeFailure=mc::loadCustomPresetLibrary(native.profileId);backend.mismatch=true;
    bool partial{};try{executePowerResource(native,{PowerWrite::Preset,{},0});}catch(const std::runtime_error&){partial=true;}
    check(partial&&mc::loadCustomPresetLibrary(native.profileId).selected==beforeFailure.selected,"failed power step persisted a successful preset selection");backend.mismatch=false;
    CustomClickContext click;click.nativeCustom=true;click.fanDefaultLocked=[]()->bool{throw std::logic_error("foreign EC gate reached native editor");};
    check(decideCustomClick(click)==CustomClickDecision::OpenNative,"native Custom editor stayed blocked");click.editing=true;check(decideCustomClick(click)==CustomClickDecision::ApplyNative,"native Custom second click mapping");
    const auto available=nativePowerButtonAvailability({true,true,true,true,true,2});
    check(available.custom&&available.presets&&available.editors&&*available.modes[1]&&*available.modes[2]&&!*available.modes[3],"native button availability differs from admitted backend state");
    check(!nativePowerButtonAvailability({true,true,true,false,true,2}).custom,"unavailable native writer enabled Custom");
    mc::gui::SettingsSnapshot snapshot;snapshot.valid=true;snapshot.oem.connected=true;
    PowerSelectionContext selection{snapshot,{20,25,30}};selection.oemControl=true;selection.nativeCustom=true;selection.customIndex=4;
    snapshot.oem.customCpuPowerActive=true;check(selectedPowerMode(selection)==4,"confirmed native Custom did not highlight");
    snapshot.oem.customCpuPowerActive=false;snapshot.oem.stockCpuProfile=0;check(selectedPowerMode(selection)==1,"native Stock inherited an old Custom selection");
    snapshot.oem.stockCpuProfile.reset();check(selectedPowerMode(selection)==-1,"unknown native limits inferred an Honor profile");
    const auto powerPath=mc::portableSettingsPath("hpbsd-power-mode.ini");check(!std::filesystem::exists(powerPath),"unexpected personal power preference in fixture");
    {std::ofstream f(powerPath);f<<"HPBSD_POWER_MODE 1 0\n";}
    check(savedHpbsdPowerChoice()==mc::CpuPowerChoice{0,false,{}},"legacy native choice did not migrate logically");
    saveHpbsdCustomPower({5,70,255},1);
    check(savedHpbsdPowerChoice()==mc::CpuPowerChoice{1,true,{5,70,255}}&&savedHpbsdPowerProfile()==1,"Custom limits/native base not preserved for restore");
    saveHpbsdPowerProfile(0);check(savedHpbsdPowerChoice()==mc::CpuPowerChoice{0,false,{}},"Stock did not deactivate saved Custom");
    // Existing ConfigurationRestore handles both startup and resume with
    // fake device factories and deterministic SMBIOS. No real driver is opened.
    mc::BoardDescriptor board;board.manufacturer="Micro Computer (HK) Tech Limited";board.baseBoard="HPBSD";board.productName="EliteMini Series";mc::tests::setTestBoard(board);
    const auto fanPath=mc::portableSettingsPath("hpbsd-fan-profiles.csv");
    mc::FanCurveProfiles intent;intent.setActivePointCount(0,8);intent.setActivePointCount(1,3);intent.curves[0]=second;intent.selected.fill(mc::FanCurvePreset::Manual);intent.restoreSelected=true;
    std::string error;check(mc::saveFanCurveProfiles(fanPath,intent,error),"shared FAN intent not saved");
    mc::FanCurveProfiles reread;check(mc::loadFanCurveProfiles(fanPath,reread,error)&&reread==intent,"shared16-cell/count/selection round-trip lost data");
    const auto bytes=[&]{std::ifstream in(fanPath,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};const auto beforeRestore=bytes();
    std::mutex observedGate;std::condition_variable changed;int replayed{};
    const auto cpuFactory=[&]()->std::unique_ptr<mc::LaptopControl>{auto value=std::make_unique<FakeCpu>();value->nativeCustom=true;value->choice=mc::CpuPowerChoice{0,true,{20,25,30}};value->applied=[&]{std::lock_guard lock(observedGate);++replayed;changed.notify_all();};return value;};
    const auto wait=[&](int count){std::unique_lock lock(observedGate);return changed.wait_for(lock,std::chrono::seconds(5),[&]{return replayed>=count;});};
    for(const auto reason:{"startup","power_resume"}){
        fans->state.manual=false;fans->state.mode=0;fans->state.curves->appliedCurve=false;
        ConfigurationRestore restore("minisforum-hpbsd",cpuFactory,[&]{return fans;});
        if(std::string(reason)=="power_resume")restore.suspend();
        const int expected=replayed+1;restore.request(reason);check(wait(expected),"saved Manual/Custom not restored through common lifecycle");
    }
    check(fans->state.mode==1&&fans->state.curves->draft==second&&bytes()==beforeRestore,"restore changed saved curves/intent or missed Manual");
    const auto writes=fans->curveWrites;
    {ConfigurationRestore restore("minisforum-hpbsd",cpuFactory,[&]{return fans;});restore.request("startup");restore.suspend();}
    check(fans->curveWrites==writes,"suspended/cancelled replay wrote FANs");
    std::filesystem::remove(fanPath);mc::tests::resetTestBoard();
    std::filesystem::remove(powerPath);
    std::filesystem::remove(path);
    std::cout<<"PASS: native GUI/CLI/restore route, confirmation before persistence, supply rejection, slot admission, "<<models<<" model declarations\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
