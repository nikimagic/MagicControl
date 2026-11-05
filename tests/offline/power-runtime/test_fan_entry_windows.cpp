#include "../../../src/platform/windows/FanDeviceActionsWindows.h"
#include "../../../src/platform/windows/ConfigurationRestoreWindows.h"
#include "../../../src/platform/windows/SettingsSnapshotWindows.h"
#include "../../../tests/TestBoardFixture.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/Logging.h"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
using namespace mc;using namespace mc::windows;using namespace std::chrono_literals;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Native final:NativeFixedFans {
    NativeFixedFanState state;std::vector<std::string> events;bool failWrite{},failRead{},wrongReadback{},lost{},flagMaximum{};std::optional<NativeFanCurves> saved;
    Native(){state.writable=true;state.minimumSupported=true;state.minimumPercent={36,38};}
    NativeFixedFanState read()override{events.push_back("read");if(failRead)throw std::runtime_error("read failed");return state;}
    void setFixed(const std::array<int,2>& target)override{events.push_back("fixed");if(failWrite)throw std::runtime_error("write failed");state.manual=true;state.targetPercent=wrongReadback?std::array<int,2>{5,5}:target;}
    void setMaximum()override{if(!flagMaximum){NativeFixedFans::setMaximum();return;}events.push_back("full-speed");if(failWrite)throw std::runtime_error("flag write failed");state.manual=true;state.targetRamped=true;state.mode=wrongReadback?0:2;}
    NativeFanAutoResult setAutomatic()override{events.push_back("auto");state.manual=false;return lost?NativeFanAutoResult::OwnershipLost:NativeFanAutoResult::Applied;}
    void setCurves(const NativeFanCurves& value)override{events.push_back("curves");state.manual=true;state.curves=NativeFanCurveState{};state.curves->draft=value;state.curves->appliedCurve=true;}
    void saveCurveDraft(const NativeFanCurves& value)override{events.push_back("save-draft");saved=value;}
};
struct Memory {std::recursive_mutex gate;std::array<std::uint8_t,65536> bytes{};Memory(){
    bytes[ec::kBoardEcVersion]=82;bytes[ec::kBoardSystemId]=72;bytes[ec::kBoardProjectId]=26;bytes[ec::kRamTableCapability]=0x40;
    bytes[ec::kPerformanceMode]=0xa0;bytes[0xf10]=19;bytes[0xf40]=21;bytes[0x7c5]=3;
}};
struct Backend final:IEcBackend {
    std::shared_ptr<Memory> memory;BackendInfo descriptor;
    explicit Backend(std::shared_ptr<Memory> m):memory(std::move(m)){descriptor.id="windows-acpi-driver";descriptor.writable=true;descriptor.maxAddress=0xffff;}
    const BackendInfo& info()const noexcept override{return descriptor;}
    std::uint8_t read(std::uint16_t address)override{std::lock_guard lock(memory->gate);return memory->bytes[address];}
    void write(std::uint16_t address,std::uint8_t value)override{std::lock_guard lock(memory->gate);memory->bytes[address]=value;}
    void lockTransaction()override{memory->gate.lock();}void unlockTransaction()noexcept override{memory->gate.unlock();}
};
struct Owner final:FanOwnership {void restoreService()override{}};
struct Environment final:FanControlEnvironment {
    MonitoringSnapshot readTemperatures()override{MonitoringSnapshot s;s.processorSampledAtMs=GetTickCount64();s.processors[0].celsius=45;s.processors[1].celsius=40;return s;}
    std::unique_ptr<FanOwnership> acquireOwnership(bool)override{return std::make_unique<Owner>();}
    FanPowerTime powerTime()const noexcept override{const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());return {now,now};}
};
}
int main(){try{
    setLoggingEnabled(false);Native native;FanWriteRequest request{FanResourceOperation::Manual};request.target=std::array<int,2>{5,100};
    auto result=executeFanResource({nullptr,&native},request);
    check(result.success&&native.events==std::vector<std::string>({"fixed","read"})&&native.state.targetPercent==*request.target,"native command/readback path or5/100 bounds changed");
    native.events.clear();native.lost=true;request.operation=FanResourceOperation::Default;result=executeFanResource({nullptr,&native},request);
    check(!result.success&&native.events==std::vector<std::string>({"auto","read"}),"ownership lost was falsely confirmed by auto-looking readback");native.lost=false;
    request.operation=FanResourceOperation::Manual;native.failWrite=true;native.failRead=true;native.events.clear();result=executeFanResource({nullptr,&native},request);
    check(!result.success&&!result.native&&result.error.find("write failed")!=std::string::npos&&result.error.find("read failed")!=std::string::npos,"command/read errors or absence were lost");
    native.failWrite=native.failRead=false;native.wrongReadback=true;result=executeFanResource({nullptr,&native},request);check(!result.success,"wrong target readback confirmed");native.wrongReadback=false;
    native.events.clear();request.operation=FanResourceOperation::Minimum;result=executeFanResource({nullptr,&native,nullptr,{},true},request);
    check(result.success&&native.state.targetPercent==std::array<int,2>{36,38}&&native.events==std::vector<std::string>({"read","fixed","read"}),"Min did not use fresh native minimum");
    NativeFanCurves curves{};curves[0][0]={20,5};curves[0][7]={100,100};curves[1][2]={83,100};const auto original=curves;
    request.operation=FanResourceOperation::Manual;request.profiles=mc::FanCurveProfiles{};request.profiles->curves[0]=curves;result=executeFanResource({nullptr,&native,nullptr,{},true},request);
    check(result.success&&curves==original&&native.state.curves&&native.state.curves->draft==curves,"native curve payload changed");
    for(const auto origin:{FanWriteOrigin::Explicit,FanWriteOrigin::Restore,FanWriteOrigin::Compound}){
        native.flagMaximum=true;native.events.clear();native.state.curves->appliedCurve=false;
        const auto retained=native.state.curves->draft;request.operation=FanResourceOperation::Maximum;request.origin=origin;
        result=executeFanResource({nullptr,&native},request);
        check(result.success&&native.events==std::vector<std::string>({"full-speed","read"})&&native.state.curves->draft==retained,"Max/restore with saved profiles bypassed native flag or changed Manual");
    }
    native.wrongReadback=true;native.events.clear();result=executeFanResource({nullptr,&native},request);
    check(!result.success&&native.events==std::vector<std::string>({"full-speed","read"}),"missing native Full Speed flag was confirmed");native.wrongReadback=false;
    native.failWrite=true;native.events.clear();result=executeFanResource({nullptr,&native},request);
    check(!result.success&&native.events==std::vector<std::string>({"full-speed","read"}),"failed native Max fell back to setFixed/setCurves");native.failWrite=false;
    native.flagMaximum=false;request={FanResourceOperation::Maximum};native.state.targetRamped=false;native.events.clear();result=executeFanResource({nullptr,&native},request);
    check(result.success&&native.events==std::vector<std::string>({"fixed","read"})&&native.state.targetPercent==std::array<int,2>{100,100},"existing fixed-PWM/RPM Max changed");
    request={FanResourceOperation::SaveDraft};request.profiles=mc::FanCurveProfiles{};request.profiles->curves[0]=curves;
    native.events.clear();request.operation=FanResourceOperation::SaveDraft;result=executeFanResource({nullptr,&native},request);
    check(result.success&&native.saved==curves&&native.events==std::vector<std::string>{"save-draft"},"draft save touched hardware read/apply");
    int absent{};for(const auto* profile:deviceProfiles()){static_cast<void>(profile);result=executeFanResource({},FanWriteRequest{FanResourceOperation::Maximum});check(!result.success&&!result.pending,"catalogue granted missing FAN access");++absent;}
    // Existing core/transport through memory only; no real owner, SDK or driver.
    tests::resetTestBoard();auto memory=std::make_shared<Memory>();EcAccess ec(std::make_unique<Backend>(memory));check(ec.verifyBoard().verified,"fake Wujie identity failed");
    FanController controller(ec,std::make_unique<Environment>());
    auto operation=deviceOperations().tryBegin();check(bool(operation),"test operation unavailable");
    FanResourceContext context{nullptr,nullptr,&controller,operation};FanCurveTable curve;for(auto& point:curve)point={255,100};curve[0]={32,25};curve[1]={45,50};curve[2]={60,100};
    request={FanResourceOperation::Manual};request.ecCurve=curve;request.wait=true;request.origin=FanWriteOrigin::Compound;
    result=executeFanResource(context,request);check(result.success&&result.ec&&result.ec->ecManual,"shared EC Manual did not complete under caller lease");
    check(memory->bytes[0xf10]==19&&memory->bytes[0xf40]==21&&(memory->bytes[0x7c5]&7)==3,"Manual changed firmware Down0 or keyboard pulse bits");
    request={FanResourceOperation::Maximum};request.wait=true;result=executeFanResource(context,request);check(result.success&&result.ec->maximum,"Max fought its own operation lease");
    FanCurveProfiles profiles;profiles.ecManual=false;profiles.linked=false;profiles.selected.fill(FanCurvePreset::Manual);profiles.curves[static_cast<unsigned>(FanCurvePreset::Manual)][0]=curve;profiles.curves[static_cast<unsigned>(FanCurvePreset::Manual)][1]=curve;
    request={FanResourceOperation::Online};request.profiles=profiles;request.wait=true;result=executeFanResource(context,request);
    check(result.success&&result.ec&&!result.ec->ecManual&&result.ec->controlled[0]&&result.ec->controlled[1],"Online conflated autonomous Manual or lost a channel");
    {std::lock_guard lock(memory->gate);memory->bytes[0x728]|=4;memory->bytes[ec::kAppPresence]|=1;}
    request={FanResourceOperation::Default};request.wait=true;request.origin=FanWriteOrigin::Restore;result=executeFanResource(context,request);
    check(result.success&&(memory->bytes[0x728]&4)&&(memory->bytes[ec::kAppPresence]&1),"internal Default released unrelated APP consumers");
    const auto keys=portableSettingsPath("keys-settings.ini");check(!std::filesystem::exists(settingsReadPath(keys)),"fixture must not have personal KEYs settings");
    struct Cleanup{std::filesystem::path file;~Cleanup(){std::error_code error;std::filesystem::remove(file,error);}} cleanup{keys};
    request.origin=FanWriteOrigin::Explicit;result=executeFanResource(context,request);
    check(result.success&&!(memory->bytes[0x728]&4)&&!(memory->bytes[ec::kAppPresence]&1)&&(memory->bytes[ec::kPerformanceMode]&0xb0)==0xa0,"explicit Default did not reset APP consumers or changed CPU policy");
    // Memory EC only: an old restore waiting on our lease must not write after
    // sleep cancels it. The fake controller is destroyed after the restorer.
    {
        auto sharedController=std::shared_ptr<FanController>(&controller,[](FanController*){});
        ConfigurationRestore restore(ec,sharedController);
        const auto before=ec.writeCount();restore.request("startup");
        std::this_thread::sleep_for(1200ms);
        restore.suspend();operation.reset();context.operation.reset();
        std::this_thread::sleep_for(300ms);
        check(ec.writeCount()==before,"cancelled pre-sleep restore wrote to fake EC");
    }
    // Restore the saved Manual/Custom through the actual configuration owner.
    // Accepted-limit bytes are controlled separately by this simulated EC;
    // they are not an SMU measurement and must not be inferred from overrides.
    {
        const auto config=portableSettingsPath("device-settings.ini");
        const auto fanFile=portableSettingsPath("fan-curves.csv");
        check(!std::filesystem::exists(config)&&!std::filesystem::exists(fanFile),"sleep fixture requires isolated settings");
        struct CleanupFiles{std::filesystem::path a,b;~CleanupFiles(){std::error_code e;std::filesystem::remove(a,e);std::filesystem::remove(b,e);}} cleanupFiles{config,fanFile};
        DevicePreferences saved;saved[DeviceSetting::ApuMode]=4;saved[DeviceSetting::ManualStapm]=15;
        saved[DeviceSetting::ManualSlow]=25;saved[DeviceSetting::ManualFast]=45;
        std::string error;check(rememberDevicePreferences(saved,error,config),"fake Custom settings were not saved");
        FanCurveProfiles intent;intent.ecManual=true;intent.linked=true;intent.restoreSelected=true;
        intent.selected.fill(FanCurvePreset::Manual);intent.curves[static_cast<unsigned>(FanCurvePreset::Manual)][0]=curve;intent.curves[static_cast<unsigned>(FanCurvePreset::Manual)][1]=curve;
        check(saveFanCurveProfiles(fanFile,intent,error),"fake Manual settings were not saved");
        check(initializeLogging(true,portableSettingsPath("fake-restore-logs"),false),"fixture journal could not be opened");
        {std::lock_guard lock(memory->gate);memory->bytes[0x49f]|=2;memory->bytes[0x727]&=0x7f;
            memory->bytes[0x7c3]=7;memory->bytes[0x770]=0xff;
            // Invalid charge byte prevents migration from inventing a saved
            // battery intent and entering the real Windows battery WMI path.
            memory->bytes[0x7b9]=101;
            memory->bytes[ec::kKeyboardCapability]|=ec::kKeyboardRgbBit;
            memory->bytes[0x783]=0;memory->bytes[0x784]=0;memory->bytes[0x785]=0;
            memory->bytes[0x46a]=10;memory->bytes[0x46b]=10;memory->bytes[0x46f]=20;}
        auto sharedController=std::shared_ptr<FanController>(&controller,[](FanController*){});
        ConfigurationRestore restore(ec,sharedController);restore.suspend();restore.request("power_resume");
        const auto until=std::chrono::steady_clock::now()+15s;bool restored{};
        while(std::chrono::steady_clock::now()<until){
            {std::lock_guard lock(memory->gate);restored=memory->bytes[0x783]==15&&memory->bytes[0x784]==25&&memory->bytes[0x785]==45;}
            const auto status=controller.status();if(restored&&status.ecManual&&status.owned)break;
            std::this_thread::sleep_for(50ms);
        }
        const auto status=controller.status();check(restored&&status.ecManual&&status.owned,"fake EC did not restore Manual and Custom after wake");
        bool completed{};const auto completeBy=std::chrono::steady_clock::now()+10s;
        while(std::chrono::steady_clock::now()<completeBy){
            std::ifstream input(currentLogPath(),std::ios::binary);
            const std::string log{std::istreambuf_iterator<char>(input),{}};
            if(log.find("RESTORE settings confirmed: reason=power_resume")!=std::string::npos){completed=true;break;}
            if(log.find("RESTORE settings incomplete: reason=power_resume")!=std::string::npos)break;
            std::this_thread::sleep_for(50ms);
        }
        check(completed,"fake EC restore did not reach full settings confirmation");
        auto observed=readEcPerformance(ec);
        check(observed.acceptedValid&&observed.accepted.sustained==10&&observed.accepted.shortBoost==10&&observed.accepted.peak==20,
            "old accepted limits were confused with Custom request");
        mc::gui::SettingsSnapshot sample;sample.valid=true;sample.power=observed;
        sample.apu.stapmLimitWatts=12.;sample.apu.slowLimitWatts=18.;sample.apu.fastLimitWatts=25.;
        check(mc::gui::observedSettingsPowerLimits(sample,false,true)==std::array<std::optional<double>,3>{10.,10.,20.},
            "fake SMU or requested values overwrote old accepted EC limits");
        {std::lock_guard lock(memory->gate);memory->bytes[0x46a]=15;memory->bytes[0x46b]=25;memory->bytes[0x46f]=45;}
        observed=readEcPerformance(ec);
        check(observed.acceptedValid&&observed.accepted.sustained==15&&observed.accepted.shortBoost==25&&observed.accepted.peak==45,
            "updated fake EC accepted limits were not observed");
        sample.power=observed;check(mc::gui::observedSettingsPowerLimits(sample,false,true)==std::array<std::optional<double>,3>{15.,25.,45.},
            "fresh accepted EC limits were not displayed independently of fake SMU");
        {std::lock_guard lock(memory->gate);memory->bytes[0x46a]=0;}
        check(!readEcPerformance(ec).acceptedValid,"unavailable accepted limit was falsely confirmed");
        {std::lock_guard lock(memory->gate);memory->bytes[0x46a]=15;memory->bytes[ec::kBoardProjectId]=25;}
        check(!readEcPerformance(ec).acceptedValid,"foreign EC identity produced confirmed Wujie limits");
        {std::lock_guard lock(memory->gate);memory->bytes[ec::kBoardProjectId]=26;}
    }
    tests::resetTestBoard();
    std::cout<<"PASS: Native ownership/readback/failures/draft, Wujie Manual/Online/Max/Default lease and "<<absent<<" missing-backend declarations; memory only\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
