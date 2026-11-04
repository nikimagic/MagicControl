#include "MagicControl/EcAccess.h"
#include "MagicControl/Errors.h"
#include <windows.h>
#include "MagicControl/EcRegister.h"
#include "MagicControl/Logging.h"
#include "../src/platform/windows/FanControlWindows.h"
#include "../src/platform/windows/FanDeviceActionsWindows.h"
#include "../src/platform/windows/WindowsDevices.h"
#include "../src/platform/windows/RadioPermission.h"
#include "../src/platform/windows/CustomModeWindows.h"
#include "../src/gui/windows/SettingsPage.h"
#include "../src/gui/windows/OsdPopup.h"
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
namespace {
int failures{};
const char* testStage="startup";
#define EXPECT(value) do { if (!(value)) {++failures;std::printf("FAIL line %d: %s\n",__LINE__,#value);} } while(false)
struct Memory {
    std::recursive_mutex mutex;
    std::array<std::uint8_t,65536> bytes{};
    std::atomic<bool> block{}, entered{};
    std::atomic<int> acquired{}, restored{};
    std::atomic<int> presenceReads{}, keyboardWrites{};
    std::atomic<int> windowsToggles{};
    std::atomic<bool> failRestore{}, failOwnership{}, mutateShared{}, temperaturesUnavailable{};
    std::atomic<int> customWrites{}, reads{};
    std::atomic<int> projectReads{};
    std::atomic<int> failReadAddress{-1};
    std::atomic<bool> blockBatteryRead{},batteryReadEntered{};
    std::atomic<bool> blockRomRead{},romReadEntered{};
    std::atomic<int> batteryReads{};
    std::atomic<std::uint64_t> simulatedSleepMs{};
    std::atomic<int> rgbWrites{}, staticColorApplies{};
    Memory() {
        bytes[mc::ec::kBoardEcVersion]=82; bytes[mc::ec::kBoardSystemId]=72; bytes[mc::ec::kBoardProjectId]=26;
        bytes[mc::ec::kRamTableCapability]=0x40;bytes[mc::ec::kPerformanceMode]=0xA0;
        bytes[mc::ec::kCpuRpmHigh]=0x0B;bytes[mc::ec::kCpuRpmLow]=0xB8;
        bytes[mc::ec::kGpuRpmHigh]=0x0A;bytes[mc::ec::kGpuRpmLow]=0xF0;
    }
};
class Backend final : public mc::IEcBackend {
    std::shared_ptr<Memory> memory_; mc::BackendInfo info_;
public:
    explicit Backend(std::shared_ptr<Memory> memory):memory_(std::move(memory)){
        // Match the transport identity required by the real profile while all
        // reads/writes below still use this test's byte array, never the driver.
        info_.id="windows-acpi-driver";info_.writable=true;info_.maxAddress=0xFFFF;
    }
    const mc::BackendInfo& info() const noexcept override{return info_;}
    std::uint8_t read(std::uint16_t a) override {
        if(a==0x770&&memory_->blockRomRead){memory_->romReadEntered=true;const auto until=std::chrono::steady_clock::now()+5s;
            while(memory_->blockRomRead&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);}
        if(a==mc::ec::kBatteryChargeControl){++memory_->batteryReads;
            if(memory_->blockBatteryRead){memory_->batteryReadEntered=true;const auto until=std::chrono::steady_clock::now()+5s;
                while(memory_->blockBatteryRead&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);}}
        std::lock_guard lock(memory_->mutex);++memory_->reads;
        if(a==mc::ec::kBoardProjectId)++memory_->projectReads;
        if(memory_->failReadAddress==a)throw std::runtime_error("injected EC read failure");
        if(a==mc::ec::kAppPresence)++memory_->presenceReads;return memory_->bytes[a];}
    void write(std::uint16_t a,std::uint8_t v) override {
        std::lock_guard lock(memory_->mutex);memory_->bytes[a]=v;
        if(a>=mc::ec::kKeyboardRed&&a<=mc::ec::kKeyboardBlue)++memory_->rgbWrites;
        if(a==0x726) { ++memory_->customWrites; if(memory_->mutateShared) memory_->bytes[a]|=2; }
        if(a==0xF20)memory_->bytes[mc::ec::kCpuPwm]=v;
        if(a==0xF50)memory_->bytes[mc::ec::kGpuPwm]=v;
        if(a==mc::ec::kPerformanceMode && (v&mc::ec::kPerformanceBoostBit)) {
            memory_->bytes[mc::ec::kCpuPwm]=200;memory_->bytes[mc::ec::kGpuPwm]=200;
        }
        if(a==mc::ec::kAppPresence && (v&1) && (memory_->bytes[mc::ec::kRamTableCommit]&4)) {
            // Firmware re-latches an unchanged RAM table on the AP apply edge.
            memory_->bytes[mc::ec::kCpuPwm]=memory_->bytes[0xF20];
            memory_->bytes[mc::ec::kGpuPwm]=memory_->bytes[0xF50];
        }
        if(a==mc::ec::kKeyboardLevel) {
            ++memory_->keyboardWrites;memory_->bytes[a]&=static_cast<std::uint8_t>(~mc::ec::kKeyboardStrobe);
        }
        if(a==mc::ec::kKeyboardEffect && (v&1)) {
            ++memory_->windowsToggles;memory_->bytes[mc::ec::kWindowsKeyLock]^=1;
            memory_->bytes[a]&=0xFE;
        }
        if(a==mc::ec::kKeyboardEffect && (v&0x20)) {
            ++memory_->staticColorApplies;
            memory_->bytes[a]&=0xDF;
        }
    }
    void lockTransaction() override{memory_->mutex.lock();}
    void unlockTransaction() noexcept override{memory_->mutex.unlock();}
};
class Owner final : public mc::windows::FanOwnership {
    std::shared_ptr<Memory> memory_;
public:
    explicit Owner(std::shared_ptr<Memory> memory):memory_(std::move(memory)){++memory_->acquired;}
    void restoreService() override{++memory_->restored;if(memory_->failRestore)throw std::runtime_error("Injected restoration failure");}
};
class Environment final : public mc::windows::FanControlEnvironment {
    std::shared_ptr<Memory> memory_;
public:
    explicit Environment(std::shared_ptr<Memory> memory):memory_(std::move(memory)){}
    mc::windows::FanPowerTime powerTime()const noexcept override {
        const auto awake=GetTickCount64();return {awake+memory_->simulatedSleepMs.load(),awake};
    }
    mc::MonitoringSnapshot readTemperatures() override {
        memory_->entered=true;
        const auto until=std::chrono::steady_clock::now()+5s;
        while(memory_->block && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(1ms);
        mc::MonitoringSnapshot sample;sample.processorSampledAtMs=GetTickCount64();
        if(memory_->temperaturesUnavailable)return sample;
        sample.processors[0].celsius=45;sample.processors[1].celsius=40;return sample;
    }
    std::unique_ptr<mc::windows::FanOwnership> acquireOwnership(bool) override{
        if(memory_->failOwnership)throw std::runtime_error("Another owner");
        return std::make_unique<Owner>(memory_);
    }
};
template<class Check> bool wait(Check check, std::chrono::milliseconds timeout=4s) {
    const auto until=std::chrono::steady_clock::now()+timeout;
    for (;;) {
        // A transient true is already success. Evaluating the predicate again
        // after leaving the loop can turn that observed success into failure.
        if(check())return true;
        if(std::chrono::steady_clock::now()>=until)return false;
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        std::this_thread::sleep_for(5ms);
    }
}
HWND settingsPageWindow(HWND parent){
    HWND result{};
    // The common page mechanics place SETs inside a viewport. Search its
    // descendants rather than assuming the old direct-child window layout.
    EnumChildWindows(parent,[](HWND window,LPARAM data)->BOOL{
        wchar_t name[128]{};GetClassNameW(window,name,128);
        if(wcscmp(name,L"MagicControl.SettingsPage"))return TRUE;
        *reinterpret_cast<HWND*>(data)=window;return FALSE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
}
int runTests(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);mc::setLoggingEnabled(false);
    {int observations=0;EXPECT(wait([&]{return ++observations==1;},1ms));EXPECT(observations==1);}
    if(argc==2&&std::string_view(argv[1])=="--presence-after-sleep"){
        int immediate=0,delayed=0;
        for(bool maximum:{false,true})for(int iteration=0;iteration<5;++iteration){
            auto memory=std::make_shared<Memory>();mc::EcAccess access(std::make_unique<Backend>(memory));
            if(!access.verifyBoard().verified){std::puts("SKIP: host does not match Wujie test profile");return 2;}
            mc::windows::FanController controller(access,std::make_unique<Environment>(memory));
            if(maximum)controller.maximum();else{mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};controller.apply(0,curve,true);}
            EXPECT(wait([&]{return controller.status().confirmed[0]&&!controller.status().busy&&!mc::deviceOperations().busy();}));
            controller.suspendPower();std::this_thread::sleep_for(250ms);
            {std::lock_guard lock(memory->mutex);memory->bytes[mc::ec::kAppPresence]&=0xfe;memory->bytes[mc::ec::kPerformanceMode]=0x10;}
            controller.resumePower();
            const auto recoveryStarted=std::chrono::steady_clock::now();
            const bool recovered=wait([&]{auto s=controller.status();return s.owned&&!s.busy&&!s.error&&s.confirmed[0]&&!mc::deviceOperations().busy()&&!access.observationsPaused();},8s);
            if(!recovered){const auto s=controller.status();const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-recoveryStarted).count();
                std::printf("APP recovery diagnostic maximum=%d iteration=%d elapsed_ms=%lld owned=%d busy=%d error=%d confirmed=%d gate=%d paused=%d status=%s\n",
                maximum,iteration,static_cast<long long>(ms),s.owned,s.busy,s.error,s.confirmed[0],mc::deviceOperations().busy(),access.observationsPaused(),s.message.c_str());}
            EXPECT(recovered);
            if(access.readStable(mc::ec::kAppPresence)&1)++immediate;
            else {const auto started=std::chrono::steady_clock::now();
                if(wait([&]{return (access.readStable(mc::ec::kAppPresence)&1)!=0;},4s)){
                    ++delayed;const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
                    std::printf("APP presence delayed: maximum=%d iteration=%d elapsed_ms=%lld\n",maximum,iteration,static_cast<long long>(ms));
                }else {std::printf("APP presence missing maximum=%d iteration=%d status=%s\n",maximum,iteration,controller.status().message.c_str());++failures;}}
        }
        std::printf("APP presence after simulated sleep: immediate=%d delayed=%d failures=%d; fake EC only\n",immediate,delayed,failures);
        return failures?1:0;
    }
    if(argc==2&&std::string_view(argv[1])=="--suspend-gate-stress"){
        for(int iteration=0;iteration<12;++iteration){
            auto memory=std::make_shared<Memory>();mc::EcAccess access(std::make_unique<Backend>(memory));
            if(!access.verifyBoard().verified){std::puts("SKIP: host does not match Wujie test profile");return 2;}
            mc::windows::FanController controller(access,std::make_unique<Environment>(memory));
            mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};controller.apply(0,curve,true);
            EXPECT(wait([&]{return controller.status().confirmed[0]&&!controller.status().busy&&!mc::deviceOperations().busy();}));
            memory->entered=false;memory->block=true;auto edited=curve;edited[1].duty=50;
            controller.apply(0,edited,true);EXPECT(wait([&]{return memory->entered.load();}));
            controller.suspendPower();bool gateGap=false,pauseGap=false,statusGap=false;
            const auto sample=[&]{gateGap|=!mc::deviceOperations().busy();pauseGap|=!access.observationsPaused();statusGap|=!controller.status().suspended;};
            sample();memory->block=false;
            const auto deadline=std::chrono::steady_clock::now()+250ms;
            while(std::chrono::steady_clock::now()<deadline){sample();std::this_thread::sleep_for(1ms);}
            if(gateGap||pauseGap||statusGap)std::printf("Suspend stress iteration=%d gate_gap=%d pause_gap=%d status_gap=%d\n",iteration,gateGap,pauseGap,statusGap);
            EXPECT(!gateGap&&!pauseGap&&!statusGap);controller.resumePower();
            EXPECT(wait([&]{return !controller.status().busy&&!mc::deviceOperations().busy()&&!access.observationsPaused();},8s));
        }
        std::printf("Suspend gate stress: %d failures; fake EC only\n",failures);return failures?1:0;
    }
    if(argc==2&&std::string_view(argv[1])=="--service-restore-failure"){
        auto memory=std::make_shared<Memory>();mc::EcAccess access(std::make_unique<Backend>(memory));
        if(!access.verifyBoard().verified){std::puts("SKIP: host does not match Wujie test profile");return 2;}
        mc::windows::FanController controller(access,std::make_unique<Environment>(memory));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};controller.apply(0,curve,true);
        EXPECT(wait([&]{return controller.status().confirmed[0]&&!controller.status().busy;}));
        memory->failRestore=true;auto stop=controller.nativeAndWait();
        EXPECT(stop.wait_for(4s)==std::future_status::ready);
        bool failed=false;try{stop.get();}catch(const std::exception&){failed=true;}EXPECT(failed);
        EXPECT(wait([&]{return controller.status().needsRestore&&!mc::deviceOperations().busy();}));
        bool refused=false;try{static_cast<void>(controller.performance(1));}catch(const std::exception&){refused=true;}EXPECT(refused);
        const auto beforeRetryWrites=access.writeCount();auto stillFailing=controller.nativeAndWait();
        EXPECT(stillFailing.wait_for(4s)==std::future_status::ready);
        bool retryFailed=false;try{stillFailing.get();}catch(const std::exception&){retryFailed=true;}
        EXPECT(retryFailed&&wait([&]{return controller.status().needsRestore&&!mc::deviceOperations().busy();}));
        EXPECT(access.writeCount()==beforeRetryWrites);
        const auto restoreAttempts=memory->restored.load();
        memory->failRestore=false;
        if(controller.status().needsRestore){auto retry=controller.nativeAndWait();EXPECT(retry.wait_for(4s)==std::future_status::ready);retry.get();
            EXPECT(wait([&]{return !mc::deviceOperations().busy();}));EXPECT(!controller.status().needsRestore);
            EXPECT(memory->restored.load()>restoreAttempts);}
        std::printf("Service restore failure: %d failures; fake EC and service only\n",failures);return failures?1:0;
    }
    if(argc==2&&std::string_view(argv[1])=="--board-identity-after-sleep"){
      for(bool manual:{false,true})for(bool unreadable:{false,true}){
        auto memory=std::make_shared<Memory>();mc::EcAccess access(std::make_unique<Backend>(memory));
        if(!access.verifyBoard().verified){std::puts("SKIP: host does not match Wujie test profile");return 2;}
        mc::windows::FanController controller(access,std::make_unique<Environment>(memory));
        if(manual){mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};controller.apply(0,curve,true);}
        else controller.maximum();
        EXPECT(wait([&]{return controller.status().confirmed[0]&&!controller.status().busy;}));
        controller.suspendPower();std::this_thread::sleep_for(200ms);
        if(unreadable)memory->failReadAddress=mc::ec::kBoardProjectId;
        else {std::lock_guard lock(memory->mutex);memory->bytes[mc::ec::kBoardProjectId]=0;}
        const auto before=access.writeCount();controller.resumePower();
        EXPECT(wait([&]{return !access.boardVerified()&&controller.status().error;},14s));
        EXPECT(!access.boardVerified()&&access.writeCount()==before);
        bool refused=false;try{access.ensureWritable();}catch(const mc::BoardNotVerified&){refused=true;}
        EXPECT(refused);
        memory->failReadAddress=-1;
      }
        std::printf("Board identity after sleep: %d failures; fake EC only\n",failures);return failures?1:0;
    }
    if(argc==2&&std::string_view(argv[1])=="--sets-worker-start-failure"){
        auto m=std::make_shared<Memory>();m->bytes[0x7C3]=4;m->bytes[0x770]=0xff;
        m->bytes[mc::ec::kBatteryChargeControl]=95;mc::EcAccess a(std::make_unique<Backend>(m));
        if(!a.verifyBoard().verified){std::puts("SKIP: host does not match Wujie test profile");return 2;}
        const HWND parent=CreateWindowExW(0,L"STATIC",L"SETs startup failure test",0,0,0,0,0,HWND_MESSAGE,nullptr,nullptr,nullptr);
        EXPECT(parent!=nullptr);
        {
            std::atomic<bool> failNext{true};std::atomic<int> attempts{};
            mc::gui::OsdPopup popup;
            EXPECT(popup.create(GetModuleHandleW(nullptr)));
            bool pendingAtFailure=false,nonstandardFailure=false;
            auto factory=[&](std::function<void()> task)->std::thread{
                ++attempts;if(failNext.exchange(false)){
                    pendingAtFailure=popup.hasPendingCommand("battery");
                    if(nonstandardFailure)throw 42;
                    throw std::runtime_error("injected thread creation failure");
                }
                return std::thread(std::move(task));};
            mc::gui::SettingsPage settings(parent,&a,&popup,{}, {},factory);
            settings.arrange(RECT{0,0,1600,1200},96,true);
            EXPECT(attempts==1&&!settings.trayState().ready&&!mc::deviceOperations().busy()&&a.writeCount()==0);
            settings.refreshForTray();
            EXPECT(wait([&]{return settings.trayState().ready&&settings.trayState().battery==1&&!mc::deviceOperations().busy();},12s));
            const auto priorWrites=a.writeCount();const int before=attempts.load();failNext=true;
            settings.applyFromTray(mc::gui::TraySetting::Battery,2);
            EXPECT(attempts==before+1&&!mc::deviceOperations().busy()&&a.writeCount()==priorWrites);
            EXPECT(pendingAtFailure&&!popup.hasPendingCommand("battery"));
            EXPECT(settings.trayState().ready&&settings.trayState().battery==1);
            settings.refreshForTray();
            EXPECT(wait([&]{return attempts>=before+2&&settings.trayState().ready&&!mc::deviceOperations().busy();},12s));
            EXPECT(a.writeCount()==priorWrites);

            // Queue a user action behind a held passive read, then fail the
            // queued worker's creation. The same OSD token and reservation
            // must be released without invoking the queued EC writer.
            m->batteryReadEntered=false;m->blockBatteryRead=true;
            settings.refreshForTray();
            const bool held=wait([&]{return m->batteryReadEntered.load();});
            EXPECT(held);
            if(held){
                const int queuedBefore=attempts.load();pendingAtFailure=false;
                settings.applyFromTray(mc::gui::TraySetting::Battery,2);
                EXPECT(popup.hasPendingCommand("battery")&&a.writeCount()==priorWrites);
                nonstandardFailure=true;failNext=true;m->blockBatteryRead=false;
                EXPECT(wait([&]{return attempts>queuedBefore&&!mc::deviceOperations().busy();},12s));
                EXPECT(pendingAtFailure&&!popup.hasPendingCommand("battery"));
                EXPECT(a.writeCount()==priorWrites&&settings.trayState().battery==1);
                settings.refreshForTray();
                EXPECT(wait([&]{return attempts>queuedBefore+1&&settings.trayState().ready&&!mc::deviceOperations().busy();},12s));
            }else m->blockBatteryRead=false;
        }
        DestroyWindow(parent);
        std::printf("SETs worker start failure: %d failures; fake EC only\n",failures);return failures?1:0;
    }
    if(argc==2&&std::string_view(argv[1])=="--sets-wake-observation"){
        auto m=std::make_shared<Memory>();m->bytes[0x7C3]=4;m->bytes[0x770]=0xff;
        m->bytes[mc::ec::kBatteryChargeControl]=95;mc::EcAccess a(std::make_unique<Backend>(m));
        if(!a.verifyBoard().verified){std::puts("SKIP: host does not match Wujie test profile");return 2;}
        const HWND parent=CreateWindowExW(0,L"STATIC",L"SETs wake test",0,0,0,0,0,HWND_MESSAGE,nullptr,nullptr,nullptr);
        EXPECT(parent!=nullptr);
        {
            mc::gui::SettingsPage settings(parent,&a,nullptr,{},{});
            m->blockBatteryRead=true;settings.arrange(RECT{0,0,1600,1200},96,true);
            EXPECT(wait([&]{return m->batteryReadEntered.load();}));
            // Same invalidation used by MainWindow after wake. The first poll
            // is still in flight; no real sleep or EC writes are performed.
            settings.refreshForTray(true);
            {std::lock_guard lock(m->mutex);m->bytes[mc::ec::kBatteryChargeControl]=85;}
            m->blockBatteryRead=false;
            EXPECT(wait([&]{const auto s=settings.trayState();return s.ready&&s.battery==2&&m->batteryReads.load()>=4;},12s));
            // Exercise the real page and its EC logger using only the fake
            // backend: a pre-wake byte must not remain eligible for writing.
            const auto page=settingsPageWindow(parent);
            const auto address=GetDlgItem(page,240),value=GetDlgItem(page,242);
            EXPECT(page&&address&&value);
            if(page&&address&&value){
                MSG enter{};enter.hwnd=address;enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;
                EXPECT(settings.preTranslate(enter));
                EXPECT(wait([&]{wchar_t text[32]{};GetWindowTextW(value,text,32);return wcscmp(text,L"0xFF")==0&&IsWindowEnabled(value);},12s));
                // Hold a second logger read across invalidation. Completion of
                // the old operation must not repopulate the writable field.
                m->romReadEntered=false;m->blockRomRead=true;
                EXPECT(settings.preTranslate(enter));
                const bool held=wait([&]{return m->romReadEntered.load();});EXPECT(held);
                const int batteryReadsBefore=m->batteryReads.load();
                mc::BatteryReading battery;battery.present=true;battery.percent=42;battery.dynamicAtMs[0]=123;
                settings.testSeedBatteryProjection(battery);
                EXPECT(settings.testBatteryProjectionAvailable());
                settings.refreshForTray(true);
                EXPECT(!settings.testBatteryProjectionAvailable());
                wchar_t cleared[32]{};GetWindowTextW(value,cleared,32);
                EXPECT(wcscmp(cleared,L"—")==0&&!IsWindowEnabled(value));
                m->blockRomRead=false;
                EXPECT(wait([&]{return m->batteryReads.load()>=batteryReadsBefore+4&&!mc::deviceOperations().busy();},12s));
                GetWindowTextW(value,cleared,32);
                EXPECT(wcscmp(cleared,L"—")==0&&!IsWindowEnabled(value));
            }
            EXPECT(a.writeCount()==0);
        }
        DestroyWindow(parent);
        std::printf("SETs wake observation: %d failures; fake EC only\n",failures);return failures?1:0;
    }
    if(argc==2&&std::string_view(argv[1])=="--sleep-only"){
        // The injected backend and environment below keep all writes in RAM;
        // the host SMBIOS is read only for the existing exact-board guard.
        for(bool maximum:{false,true}){
            auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));
            if(!a.verifyBoard().verified){std::puts("SKIP: host does not match Wujie test profile");return 2;}
            mc::windows::FanController c(a,std::make_unique<Environment>(m));
            mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};
            if(maximum)c.maximum();else c.apply(0,curve,true);
            EXPECT(wait([&]{auto s=c.status();return !s.busy&&s.confirmed[0]&&s.confirmed[1];}));
            c.suspendPower();EXPECT(c.status().suspended&&mc::deviceOperations().busy()&&a.observationsPaused());
            std::this_thread::sleep_for(100ms);const auto writes=a.writeCount();const auto reads=m->reads.load();
            std::this_thread::sleep_for(150ms);EXPECT(a.writeCount()==writes&&m->reads==reads);
            bool refused=false;try{c.performance(2);}catch(const std::exception&){refused=true;}EXPECT(refused);
            {std::lock_guard lock(m->mutex);m->bytes[0x741]&=0xFE;m->bytes[0x751]=0x10;
                if(maximum){m->bytes[0x75B]=0;m->bytes[0x75C]=0;}}
            c.resumePower();
            EXPECT(wait([&]{auto s=c.status();return s.owned&&!s.busy&&!s.error&&s.confirmed[0]&&s.confirmed[1]&&
                !mc::deviceOperations().busy()&&!a.observationsPaused();}));
            EXPECT(c.status().maximum==maximum);if(!maximum)EXPECT(c.status().applied[0]==curve);
            std::this_thread::sleep_for(300ms);const auto restored=a.writeCount();
            c.resumePower();std::this_thread::sleep_for(100ms);EXPECT(a.writeCount()==restored);
        }
        // If no fresh sensor arrives, confirmed manual ownership is released.
        {
            auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
            mc::windows::FanController c(a,std::make_unique<Environment>(m));
            mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
            EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));
            c.suspendPower();m->temperaturesUnavailable=true;c.resumePower();
            EXPECT(wait([&]{return c.status().error&&!c.status().owned&&!mc::deviceOperations().busy();},14s));
            EXPECT(a.readStable(0x7C6)==0&&!c.status().needsRestore&&!a.observationsPaused());
        }
        // A request interrupted before confirmation cannot be replayed.
        {
            auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
            mc::windows::FanController c(a,std::make_unique<Environment>(m));
            mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};
            m->block=true;c.apply(0,curve,true);EXPECT(wait([&]{return m->entered.load();}));
            c.suspendPower();m->block=false;std::this_thread::sleep_for(100ms);c.resumePower();
            EXPECT(wait([&]{return !c.status().busy&&!mc::deviceOperations().busy();}));
            EXPECT(!c.status().owned&&m->acquired==0&&a.readStable(0x7C6)==0);
        }
        // A changed table belongs to another owner and must not be overwritten.
        {
            auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
            mc::windows::FanController c(a,std::make_unique<Environment>(m));
            mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
            EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));c.suspendPower();
            // suspendPower is signalled before the worker has necessarily
            // drained its in-flight cycle; inject the foreign change only once
            // observed reads and writes are quiescent.
            std::this_thread::sleep_for(100ms);const auto pausedWrites=a.writeCount();const auto pausedReads=m->reads.load();
            std::this_thread::sleep_for(150ms);EXPECT(a.writeCount()==pausedWrites&&m->reads==pausedReads);
            const auto beforePresence=a.readStable(0x741),beforeCommit=a.readStable(0x7C6),beforeValue=a.readStable(0xF20);
            {std::lock_guard lock(m->mutex);m->bytes[0xF20]=82;}
            const auto before=a.writeCount();c.resumePower();const bool refusedRecovery=wait([&]{return c.status().error;});
            if(!refusedRecovery){const auto status=c.status();std::printf("Recovery ownership diagnostic: owned=%d busy=%d error=%d writes=%llu/%llu F20=%u 741=%u 7C6=%u before=%u/%u/%u msg=%s\n",
                status.owned,status.busy,status.error,static_cast<unsigned long long>(a.writeCount()),static_cast<unsigned long long>(before),
                a.readStable(0xF20),a.readStable(0x741),a.readStable(0x7C6),beforeValue,beforePresence,beforeCommit,status.message.c_str());}
            EXPECT(refusedRecovery);EXPECT(a.writeCount()==before&&a.readStable(0xF20)==82&&!c.status().owned);
        }
        // An unreadable table is unknown ownership, not permission to write.
        {
            auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
            mc::windows::FanController c(a,std::make_unique<Environment>(m));
            mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
            EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));c.suspendPower();
            std::this_thread::sleep_for(250ms);m->failReadAddress=0xF20;const auto before=a.writeCount();c.resumePower();
            EXPECT(wait([&]{return c.status().error&&!c.status().owned;}));
            EXPECT(a.writeCount()==before);m->failReadAddress=-1;
        }
        std::printf("Sleep/resume: %d failures; fake EC and environment only\n",failures);return failures?1:0;
    }
    if(argc==2&&std::string_view(argv[1])=="--maximum-probe"){
        auto memory=std::make_shared<Memory>();memory->bytes[mc::ec::kAppPresence]=1;
        memory->bytes[0x783]=25;memory->bytes[0x784]=30;memory->bytes[0x785]=45;
        mc::EcAccess access(std::make_unique<Backend>(memory));EXPECT(access.verifyBoard().verified);
        mc::windows::FanController controller(access,std::make_unique<Environment>(memory));
        const auto original=memory->bytes;std::stop_source stop;
        auto result=controller.measureMaximum(stop.get_token());
        EXPECT(wait([&]{return (access.readStable(mc::ec::kPerformanceMode)&64)!=0;}));
        stop.request_stop();EXPECT(result.wait_for(4s)==std::future_status::ready);
        const auto measured=result.get();EXPECT(measured.attempted&&measured.restored&&!measured.settled);
        EXPECT(access.readStable(mc::ec::kPerformanceMode)==original[mc::ec::kPerformanceMode]);
        EXPECT(access.writeCount()==2&&memory->acquired==0&&!mc::deviceOperations().busy());
        for(auto address:{0x726,0x727,0x741,0x783,0x784,0x785,0x7C5,0x7C6})EXPECT(access.readStable(static_cast<std::uint16_t>(address))==original[address]);
        for(int address=0xF00;address<0xF60;++address)EXPECT(access.readStable(static_cast<std::uint16_t>(address))==original[address]);
        std::printf("Maximum probe actor/cancel: %d failures; fake EC only\n",failures);return failures?1:0;
    }
    if(argc==2&&std::string_view(argv[1])=="--default-observation"){
        auto memory=std::make_shared<Memory>();memory->bytes[mc::ec::kAppPresence]=1;
        mc::EcAccess access(std::make_unique<Backend>(memory));EXPECT(access.verifyBoard().verified);
        auto shared=std::make_shared<mc::windows::FanController>(access,std::make_unique<Environment>(memory));
        const HWND parent=CreateWindowExW(0,L"STATIC",L"Default observation test",0,0,0,0,0,HWND_MESSAGE,nullptr,nullptr,nullptr);
        {
            mc::gui::SettingsPage settings(parent,&access,nullptr,{},shared);
            settings.arrange(RECT{0,0,1600,1200},192,true);
            EXPECT(wait([&]{settings.refreshCommandAvailability();return settings.trayState().ready&&shared->status().observedDefault==true;}));
            const HWND page=settingsPageWindow(parent);
            const HWND office=GetDlgItem(page,111),balanced=GetDlgItem(page,112);
            // Memory starts at 0xA0: native Office. Default FAN must not
            // fabricate a separate Gaming selection or disable stock profiles.
            EXPECT(page&&office&&balanced);EXPECT(GetPropW(office,L"MC.Selected")!=nullptr);
            EXPECT(GetPropW(balanced,L"MC.Selected")==nullptr&&IsWindowEnabled(office));
            EXPECT(settings.trayState().performanceReady&&settings.trayState().performance==1);
            EXPECT(shared->status().revision==0&&!shared->status().owned);
            EXPECT(access.writeCount()==0&&memory->acquired==0);
            shared->suspendPower();EXPECT(!shared->status().observedDefault);
            shared->resumePower();
            EXPECT(wait([&]{settings.refreshCommandAvailability();return shared->status().observedDefault==true&&!shared->status().busy;}));
            settings.refreshCommandAvailability();
            EXPECT(GetPropW(office,L"MC.Selected")!=nullptr&&IsWindowEnabled(office));
            EXPECT(settings.trayState().performanceReady&&access.writeCount()==0&&memory->acquired==0);
            // A mixed flag set must not continue to advertise Default.
            {std::lock_guard lock(memory->mutex);memory->bytes[mc::ec::kCustomMode]=0x80;}
            EXPECT(wait([&]{settings.refreshCommandAvailability();return shared->status().observedDefault==false;}));
            settings.refreshCommandAvailability();EXPECT(GetPropW(office,L"MC.Selected")!=nullptr&&GetPropW(balanced,L"MC.Selected")==nullptr);
            {std::lock_guard lock(memory->mutex);memory->bytes[mc::ec::kCustomMode]=0;}
            EXPECT(wait([&]{settings.refreshCommandAvailability();return shared->status().observedDefault==true;}));
            settings.refreshCommandAvailability();EXPECT(GetPropW(office,L"MC.Selected")!=nullptr&&GetPropW(balanced,L"MC.Selected")==nullptr);
            EXPECT(access.writeCount()==0&&memory->acquired==0);
        }
        DestroyWindow(parent);
        std::printf("Default observation startup/resume: %d failures; fake EC only\n",failures);
        return failures?1:0;
    }
    // Distinguish system suspension from slow reads/writes without a heuristic
    // based on total cycle duration. A long operation advances both clocks.
    {
        mc::windows::FanSleepTracker tracker;
        EXPECT(!tracker.observe({1000,1000}));EXPECT(!tracker.observe({12000,12000}));
        EXPECT(!tracker.observe({132000,132000}));EXPECT(tracker.observe({3732000,132000}));
        EXPECT(!tracker.observe({3733000,133000}));EXPECT(!tracker.observe({0,0}));
    }
    testStage="clock-based sleep";
    // Confirmed clock-based sleep with retained flags/table. PWM is telemetry:
    // revalidate identity, but do not rewrite a table merely because it differs.
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
        EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));
        const auto before=a.writeCount();const auto identityReads=m->projectReads.load();
        {std::lock_guard lock(m->mutex);m->bytes[mc::ec::kCpuPwm]=50;m->bytes[mc::ec::kGpuPwm]=50;m->simulatedSleepMs=3600000;}
        EXPECT(wait([&]{return m->projectReads.load()>identityReads&&c.status().owned&&!c.status().busy&&!c.status().error&&c.status().confirmed[0];}));
        std::this_thread::sleep_for(200ms);EXPECT(a.writeCount()==before);
        EXPECT(c.status().applied[0]==curve&&!c.status().maximum&&m->acquired==1&&m->restored==0);
        EXPECT(a.readStable(0x7C6)&4);
    }
    testStage="queued performance during clock recovery";
    for(bool transientIdentityFailure:{false,true}){
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
        EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy&&!mc::deviceOperations().busy();}));
        // Hold the existing passive cycle, accept a real queued request, then
        // advance only the fake sleep clock. No hardware or real sleep.
        m->entered=false;m->block=true;EXPECT(wait([&]{return m->entered.load();}));
        auto operation=mc::deviceOperations().tryBegin();EXPECT(bool(operation));
        auto result=c.performance(1,operation);
        if(transientIdentityFailure)m->failReadAddress=mc::ec::kBoardProjectId;
        m->simulatedSleepMs=3600000;m->block=false;
        if(transientIdentityFailure){
            EXPECT(result.wait_for(350ms)==std::future_status::timeout);
            m->failReadAddress=-1;
        }
        EXPECT(result.wait_for(6s)==std::future_status::ready);
        if(result.wait_for(0ms)==std::future_status::ready)try{static_cast<void>(result.get());}
            catch(const std::exception& e){std::printf("Recovery command: %s\n",e.what());++failures;}
        operation.reset();
        EXPECT(wait([&]{return !c.status().applying&&!mc::deviceOperations().busy()&&!a.observationsPaused();}));
        EXPECT(c.status().applied[0]==curve&&!c.status().maximum);
        // The queue must still accept a subsequent profile, not retain busy.
        auto next=c.performance(2);EXPECT(next.wait_for(4s)==std::future_status::ready);
        if(next.wait_for(0ms)==std::future_status::ready)try{static_cast<void>(next.get());}catch(...){++failures;}
    }
    testStage="Custom CPU base preserves fan owner";
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        auto c=std::make_shared<mc::windows::FanController>(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};
        auto ready=c->ecManualAndWait(curve,{});EXPECT(ready.wait_for(4s)==std::future_status::ready);ready.get();
        EXPECT(wait([&]{return !mc::deviceOperations().busy();}));
        const auto applied=c->status().applied;const auto owners=m->acquired.load();
        std::array<std::uint8_t,96> tables;{std::lock_guard lock(m->mutex);std::copy_n(m->bytes.begin()+0xf00,96,tables.begin());}
        for(int preset:{0,2,1}){
            auto prior=c->performance(preset);EXPECT(prior.wait_for(4s)==std::future_status::ready);prior.get();
            EXPECT(wait([&]{return !mc::deviceOperations().busy();}));
            auto operation=mc::deviceOperations().tryBegin();EXPECT(bool(operation));
            mc::windows::CustomModeContext context;context.ec=&a;context.fans=c;context.operation=operation;context.wujie=true;
            const auto result=mc::windows::executePowerResource(context,{mc::windows::PowerWrite::Custom,{80,80,85}});
            EXPECT((result.limits==std::array<int,3>{80,80,85}));
            EXPECT((a.readStable(0x751)&0xB0)==0&&a.readStable(0x783)==80&&a.readStable(0x784)==80&&a.readStable(0x785)==85);
            context.operation.reset();operation.reset();EXPECT(wait([&]{return !mc::deviceOperations().busy();}));
            EXPECT(c->status().owned&&!c->status().error&&c->status().applied==applied&&m->acquired==owners);
            {std::lock_guard lock(m->mutex);EXPECT(std::equal(tables.begin(),tables.end(),m->bytes.begin()+0xf00));}
        }
    }
    testStage="display off/on";
    // Display off/on alone is not a confirmed sleep. PWM is telemetry and
    // must not trigger a table rewrite; an actual suspend remains separate.
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
        EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));
        const auto before=a.writeCount();c.sessionDisplayChanged(1);c.sessionDisplayChanged(0);c.sessionDisplayChanged(2);
        std::this_thread::sleep_for(200ms);EXPECT(c.status().owned&&!c.status().suspended&&a.writeCount()==before);
        c.sessionDisplayChanged(0);
        {std::lock_guard lock(m->mutex);m->bytes[mc::ec::kCpuPwm]=50;m->bytes[mc::ec::kGpuPwm]=50;}
        c.sessionDisplayChanged(1);
        std::this_thread::sleep_for(1200ms);EXPECT(a.writeCount()==before&&c.status().owned&&!c.status().busy&&!c.status().error);
        EXPECT(c.status().applied[0]==curve&&!c.status().maximum);
        // Display-on also resumes a real suspend if its resume message was lost.
        c.sessionDisplayChanged(0);c.suspendPower();c.sessionDisplayChanged(1);
        EXPECT(wait([&]{return !c.status().suspended&&!c.status().applying&&!mc::deviceOperations().busy();}));
        EXPECT(c.status().owned&&!c.status().error);
    }
    // Consent starts on the caller, finishes before the device write, caches
    // Allowed only, and is requested again after denial/revocation.
    {
        mc::windows::RadioPermission permission; int began{},finished{}; bool allow=false;
        auto begin=[&]() -> mc::windows::RadioPermission::Completion {
            ++began; return [&] { ++finished; if(!allow)throw std::runtime_error("Denied"); };
        };
        bool denied=false;auto finish=permission.request(begin);EXPECT(began==1&&finished==0);
        try{finish();}catch(const std::exception&){denied=true;}EXPECT(denied);
        try{permission.ensureAllowed();EXPECT(false);}catch(const std::exception&){}
        allow=true;finish=permission.request(begin);finish();permission.ensureAllowed();
        EXPECT(began==2&&finished==2);permission.request(begin)();EXPECT(began==2&&finished==2);
        permission.revoke();permission.request(begin)();EXPECT(began==3&&finished==3);
    }
    testStage="standalone power mode";
    // Standalone mode clears the same overrides as GUI, and refuses retained
    // Manual/Max and another owner without writing any EC byte.
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        Environment source(m);
        for(int preset:{0,1,2}) {
            {std::lock_guard lock(m->mutex);m->bytes[0x783]=90;m->bytes[0x784]=90;m->bytes[0x785]=100;}
            const auto result=mc::windows::selectStandalonePerformance(a,preset,&source);
            EXPECT(result.after==(preset==0?0xA0:preset==1?0:0x10));
            EXPECT(a.readStable(0x783)==0&&a.readStable(0x784)==0&&a.readStable(0x785)==0);
        }
        for(bool maximum:{false,true}) {
            {std::lock_guard lock(m->mutex);m->bytes[0x7C6]=maximum?0:4;m->bytes[0x751]=maximum?0xE0:0xA0;}
            const auto before=a.writeCount();bool refused=false;
            try{mc::windows::selectStandalonePerformance(a,0,&source);}catch(const std::exception&){refused=true;}
            EXPECT(refused&&a.writeCount()==before);
        }
        m->failOwnership=true;const auto before=a.writeCount();bool refused=false;
        try{mc::windows::selectStandalonePerformance(a,0,&source);}catch(const std::exception&){refused=true;}
        EXPECT(refused&&a.writeCount()==before);
    }
    testStage="masked shared-bit write";
    // A firmware-owned bit changing during a masked write is not a failure.
    {
        auto m=std::make_shared<Memory>();m->mutateShared=true;
        mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};
        c.apply(0,curve,true);
        EXPECT(wait([&]{auto s=c.status();return !s.busy&&s.confirmed[0]&&s.confirmed[1];}));
        EXPECT(!c.status().error&&a.readStable(0x726)==0x82&&m->customWrites==1);
        auto stop=c.nativeAndWait();EXPECT(stop.wait_for(4s)==std::future_status::ready);stop.get();
        EXPECT(a.readStable(0x726)==2);
    }
    testStage="suspend and interrupted command";
    // Sleep pauses EC activity. Resume restores our confirmed intent and
    // profile once, retaining the original lease and waiting for fresh sensors.
    for(bool maximum:{false,true}) {
        // A completed controller result can precede release of the shared
        // operation reservation. Wait for both before issuing the next test
        // command; do not weaken the production exclusion gate.
        EXPECT(wait([]{return !mc::deviceOperations().busy();}));
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};
        if(maximum)c.maximum();else c.apply(0,curve,true);
        EXPECT(wait([&]{auto s=c.status();return !s.busy&&s.confirmed[0]&&s.confirmed[1]&&
            !mc::deviceOperations().busy()&&!a.observationsPaused();}));
        if(!maximum) {
            auto unconfirmed=curve;unconfirmed[1].duty=50;
            m->entered=false;m->block=true;c.apply(0,unconfirmed,true);
            EXPECT(wait([&]{return m->entered.load();}));
        }
        c.suspendPower();
        {const auto suspended=c.status();const bool gate=mc::deviceOperations().busy(),paused=a.observationsPaused();
            if(!suspended.suspended||!gate||!paused)std::printf("Suspend diagnostic maximum=%d suspended=%d busy=%d gate=%d paused=%d msg=%s\n",
                maximum,suspended.suspended,suspended.busy,gate,paused,suspended.message.c_str());
            EXPECT(suspended.suspended&&gate&&paused);}
        m->block=false;
        std::this_thread::sleep_for(100ms);const auto writes=a.writeCount();const auto reads=m->reads.load();
        std::this_thread::sleep_for(150ms);EXPECT(a.writeCount()==writes&&m->reads==reads);
        bool refused=false;try{c.performance(2);}catch(const std::exception&){refused=true;}EXPECT(refused);
        {std::lock_guard lock(m->mutex);m->bytes[0x741]&=0xFE;m->bytes[0x751]=0x10;
            if(maximum){m->bytes[0x75B]=0;m->bytes[0x75C]=0;}
            else {m->temperaturesUnavailable=true;m->bytes[mc::ec::kBoardProjectId]=0;}}
        c.resumePower();
        if(!maximum) {
            std::this_thread::sleep_for(200ms);EXPECT(a.writeCount()==writes&&mc::deviceOperations().busy());
            {std::lock_guard lock(m->mutex);m->bytes[mc::ec::kBoardProjectId]=26;}
            m->temperaturesUnavailable=false;
        }
        bool gateAtResume=true,pausedAtResume=true;
        const bool restored=wait([&]{auto s=c.status();gateAtResume=mc::deviceOperations().busy();pausedAtResume=a.observationsPaused();
            return s.owned&&!s.busy&&!s.error&&s.confirmed[0]&&s.confirmed[1]&&!gateAtResume&&!pausedAtResume;});
        if(!restored){const auto s=c.status();std::printf("Resume diagnostic maximum=%d owned=%d busy=%d error=%d confirmed=%d/%d gate=%d paused=%d message=%s\n",
            maximum,s.owned,s.busy,s.error,s.confirmed[0],s.confirmed[1],mc::deviceOperations().busy(),a.observationsPaused(),s.message.c_str());}
        EXPECT(restored);
        // These are the flags in the accepted completion sample. A later
        // ordinary online cycle takes its own scoped ObservationPause without
        // acquiring DeviceOperation; rereading paused here is not an invariant
        // of completed recovery. Ownership itself must remain unchanged.
        {const auto acquired=m->acquired.load(),released=m->restored.load();
            if(acquired!=1||released!=0||gateAtResume||pausedAtResume)std::printf("Resume lease diagnostic maximum=%d acquired=%d released=%d gate=%d paused=%d\n",maximum,acquired,released,gateAtResume,pausedAtResume);
            EXPECT(acquired==1&&released==0&&!gateAtResume&&!pausedAtResume);}
        // APU preset is a separate owner; only FAN boost is restored.
        EXPECT(a.readStable(0x751)==(maximum?0x50:0x10));
        // FAN mode and application presence have separate completion points;
        // require the flag within the bounded observation window.
        const bool immediatePresence=(a.readStable(0x741)&1)!=0;
        const bool eventualPresence=immediatePresence||wait([&]{return (a.readStable(0x741)&1)!=0;});
        if(!immediatePresence)std::printf("APP presence diagnostic maximum=%d eventual=%d status=%s\n",
            maximum,eventualPresence,c.status().message.c_str());
        EXPECT(eventualPresence);
        EXPECT(c.status().maximum==maximum);if(!maximum)EXPECT(c.status().applied[0]==curve&&c.status().applied[1]==curve);
        const auto after=a.writeCount();c.resumePower();std::this_thread::sleep_for(100ms);EXPECT(a.writeCount()==after);
    }
    testStage="changed identity after sleep";
    // A changed board revokes writes for every caller, including exit cleanup.
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        {mc::windows::FanController c(a,std::make_unique<Environment>(m));mc::FanCurveProfiles profiles;
            mc::FanWriteRequest request{mc::FanResourceOperation::Maximum};request.profiles=profiles;mc::windows::executeFanResource({nullptr,nullptr,&c},request).requireAccepted();
            EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));c.suspendPower();
            {std::lock_guard lock(m->mutex);m->bytes[mc::ec::kBoardProjectId]=0;}
            const auto before=a.writeCount();c.resumePower();EXPECT(wait([&]{return c.status().error;},14s));
            EXPECT(!a.boardVerified()&&a.writeCount()==before);}
        bool refused=false;try{a.writeVerified(0x741,0);}catch(const std::exception&){refused=true;}EXPECT(refused);
    }
    testStage="missing recovery temperature";
    // No fresh sensor after the recovery deadline returns our output to EC.
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
        EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));
        c.suspendPower();m->temperaturesUnavailable=true;c.resumePower();
        EXPECT(wait([&]{return c.status().error&&!c.status().owned&&!mc::deviceOperations().busy();},14s));
        EXPECT(a.readStable(0x7C6)==0&&!c.status().needsRestore&&!a.observationsPaused());
    }
    testStage="unconfirmed interrupted command";
    // A command interrupted before confirmation is not applied on resume.
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};
        m->block=true;c.apply(0,curve,true);EXPECT(wait([&]{return m->entered.load();}));
        c.suspendPower();m->block=false;
        std::this_thread::sleep_for(100ms);c.resumePower();
        EXPECT(wait([&]{return !c.status().busy&&!mc::deviceOperations().busy();}));
        EXPECT(!c.status().owned&&m->acquired==0&&a.readStable(0x7C6)==0);
    }
    testStage="foreign RAM recovery";
    // Never overwrite another active RAM table during recovery.
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
        EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));c.suspendPower();
        {std::lock_guard lock(m->mutex);m->bytes[0xF20]=82;}
        const auto before=a.writeCount();c.resumePower();EXPECT(wait([&]{return c.status().error;}));
        EXPECT(a.writeCount()==before&&a.readStable(0xF20)==82&&!c.status().owned);
    }
    testStage="external APU preset";
    // An external APU preset change is not a change of FAN ownership.
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
        EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));
        const auto before=a.writeCount();{std::lock_guard lock(m->mutex);m->bytes[0x751]=0x10;}
        std::this_thread::sleep_for(1200ms);
        EXPECT(c.status().owned&&!c.status().error&&a.writeCount()==before&&a.readStable(0x751)==0x10);
    }
    testStage="foreign FAN gate";
    // In contrast, a foreign change to the owned FAN gate must release
    // ownership without restoring tables or writing over the other owner.
    {
        auto m=std::make_shared<Memory>();mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::FanController c(a,std::make_unique<Environment>(m));
        mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};c.apply(0,curve,true);
        EXPECT(wait([&]{return c.status().confirmed[0]&&!c.status().busy;}));
        const auto before=a.writeCount();{std::lock_guard lock(m->mutex);m->bytes[0x726]&=0x7f;}
        EXPECT(wait([&]{return c.status().error&&!c.status().owned;}));
        EXPECT(a.writeCount()==before&&(a.readStable(0x726)&0x80)==0);
    }
    {
        auto m=std::make_shared<Memory>();m->bytes[0x767]=0x90;m->bytes[0x768]=0xA0;m->bytes[0x727]=0x49;
        mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        mc::windows::setWindowsKeyLocked(a,true);
        EXPECT(a.readStable(0x768)==0xA1);EXPECT(a.readStable(0x767)==0x90);EXPECT(a.readStable(0x727)==0x49);
        mc::windows::setWindowsKeyLocked(a,true);EXPECT(m->windowsToggles==1);
        mc::windows::setWindowsKeyLocked(a,false);EXPECT(m->windowsToggles==2);EXPECT(a.readStable(0x768)==0xA0);
    }
    testStage="application presence";
    // Reference application presence is independent of native cooling.
    {
        auto m=std::make_shared<Memory>();
        mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        {
            mc::windows::FanController c(a,std::make_unique<Environment>(m));
            EXPECT(wait([&]{return (a.readStable(mc::ec::kAppPresence)&1)!=0;}));
            EXPECT((a.readStable(mc::ec::kRamTableCommit)&4)==0);
            {std::lock_guard lock(m->mutex);m->bytes[mc::ec::kKeyboardCapability]=4;m->bytes[mc::ec::kKeyboardLevel]=0x80;}
            c.keyboardActivity();EXPECT(wait([&]{return m->keyboardWrites.load()==1;}));
            EXPECT(a.readStable(mc::ec::kKeyboardLevel)==0x80);
            auto native=c.nativeAndWait();EXPECT(native.wait_for(4s)==std::future_status::ready);native.get();
            EXPECT(wait([]{return !mc::deviceOperations().busy();}));
            EXPECT((a.readStable(mc::ec::kAppPresence)&1)!=0);
            c.maximum();EXPECT(wait([&]{auto s=c.status();return s.maximum&&!s.busy&&!s.error&&!mc::deviceOperations().busy();}));
            EXPECT((a.readStable(mc::ec::kAppPresence)&1)!=0);
            native=c.nativeAndWait();EXPECT(native.wait_for(4s)==std::future_status::ready);native.get();
            EXPECT(wait([]{return !mc::deviceOperations().busy();}));
            {std::lock_guard lock(m->mutex);m->bytes[mc::ec::kAppPresence]&=0xFE;}
            c.restoreApplicationPresence();EXPECT(wait([&]{return (a.readStable(mc::ec::kAppPresence)&1)!=0;}));
            EXPECT((a.readStable(mc::ec::kRamTableCommit)&4)==0);
            {std::lock_guard lock(m->mutex);m->bytes[mc::ec::kKeyboardLevel]=0x82;}
            std::this_thread::sleep_for(1100ms);c.keyboardActivity();std::this_thread::sleep_for(150ms);
            EXPECT(m->keyboardWrites==1);EXPECT(a.readStable(mc::ec::kKeyboardLevel)==0x82);
        }
        EXPECT((a.readStable(mc::ec::kAppPresence)&1)!=0); // Exit leaves APP presence untouched.
    }
    testStage="retained table presence";
    // Never activate a retained table just to restore keyboard notifications.
    {
        auto m=std::make_shared<Memory>();m->bytes[mc::ec::kRamTableCommit]=4;
        mc::EcAccess a(std::make_unique<Backend>(m));EXPECT(a.verifyBoard().verified);
        {mc::windows::FanController c(a,std::make_unique<Environment>(m));
            EXPECT(wait([&]{return m->presenceReads.load()>0;}));
            EXPECT((a.readStable(mc::ec::kAppPresence)&1)==0);EXPECT(a.readStable(mc::ec::kRamTableCommit)==4);
            const auto before=a.writeCount();c.sessionDisplayChanged(0);c.sessionDisplayChanged(1);
            std::this_thread::sleep_for(200ms);EXPECT(a.writeCount()==before&&!c.status().owned);}
        EXPECT(a.readStable(mc::ec::kRamTableCommit)==4);
    }
    testStage="SETs integration";
    auto memory=std::make_shared<Memory>();
    // The UI exercises SingleZone colours below. A zero capability byte
    // correctly disables them, so advertise RGB explicitly in this fixture.
    memory->bytes[mc::ec::kKeyboardCapability]=4;
    mc::EcAccess access(std::make_unique<Backend>(memory));EXPECT(access.verifyBoard().verified);
    auto shared=std::make_shared<mc::windows::FanController>(access,std::make_unique<Environment>(memory));
    auto& control=*shared;
    const HWND parent=CreateWindowExW(0,L"STATIC",L"SETs integration test",0,0,0,0,0,HWND_MESSAGE,nullptr,nullptr,nullptr);
    if(!parent)return 10;
    struct Root {HWND window;~Root(){DestroyWindow(window);}} root{parent};
    mc::gui::SettingsPage settings(parent,&access,nullptr,{},shared);
    settings.arrange(RECT{0,0,1600,1200},192,true);
    EXPECT(wait([&]{return settings.trayState().ready;}));
    const HWND page=settingsPageWindow(parent);
    const HWND office=GetDlgItem(page,110);
    // Starting with already-native EC flags must publish Default even though
    // no command was needed and the command revision remains zero.
    EXPECT(wait([&]{settings.refreshCommandAvailability();return control.status().observedDefault==true;}));
    settings.refreshCommandAvailability();
    EXPECT(control.status().revision==0&&!control.status().owned);
    EXPECT(office && !IsWindowEnabled(office));
    const HWND stockOffice=GetDlgItem(page,111),balanced=GetDlgItem(page,112);
    EXPECT(stockOffice&&GetPropW(stockOffice,L"MC.Selected")!=nullptr&&balanced&&GetPropW(balanced,L"MC.Selected")==nullptr);
    EXPECT(settings.trayState().performanceReady&&settings.trayState().performance==1);
    const auto idleWrites=access.writeCount();
    control.suspendPower();EXPECT(!control.status().observedDefault);
    control.resumePower();
    EXPECT(wait([&]{settings.refreshCommandAvailability();return control.status().observedDefault==true&&!control.status().busy;}));
    settings.refreshCommandAvailability();
    EXPECT(!IsWindowEnabled(office)&&GetPropW(stockOffice,L"MC.Selected")!=nullptr&&GetPropW(balanced,L"MC.Selected")==nullptr);
    EXPECT(access.writeCount()==idleWrites); // Observe, do not reapply Default/APU.
    mc::FanCurveTable curve{};curve[0]={30,20};curve[1]={40,40};curve[2]={60,100};
    memory->block=true;
    control.apply(0,curve,true);
    EXPECT(wait([&]{return memory->entered.load();}));
    EXPECT(mc::deviceOperations().busy());
    EXPECT(control.status().applying);
    settings.refreshCommandAvailability();EXPECT(!IsWindowEnabled(office));
    const auto beforeRejected=access.writeCount();
    SendMessageW(page,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),reinterpret_cast<LPARAM>(office));
    EXPECT(access.writeCount()==beforeRejected);
    bool rejected=false;
    try {static_cast<void>(control.performance(2));}catch(const std::exception&){rejected=true;}
    EXPECT(rejected);
    memory->block=false;
    EXPECT(wait([&]{auto s=control.status();return !s.applying && s.confirmed[0] && s.confirmed[1] && !mc::deviceOperations().busy();}));
    auto baseline=control.status().applied;
    // OEM breathing uses four fixed-colour presets, independent of static RGB.
    // A stale/static RGB readback must not determine the selected breathing colour.
    {
        constexpr std::array<std::array<std::uint8_t,3>,8> palette{{
            {50,50,50},{50,0,0},{50,10,0},{50,40,0},{0,50,0},{0,50,50},{0,0,50},{35,0,50}}};
        constexpr std::array<int,8> presets{1,2,0,0,3,0,4,0};
        const auto upper=static_cast<std::uint8_t>(access.readStable(mc::ec::kFanControlRespective)&0xF8);
        const auto command=[&](int id) {
            SendMessageW(page,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(page,id)));
            EXPECT(wait([&]{return settings.trayState().ready&&!mc::deviceOperations().busy();}));
        };
        command(120);command(130); // Static white, then enable native breathing.
        command(121);
        EXPECT(IsWindowEnabled(GetDlgItem(page,151))==FALSE);
        for(int color=0;color<8;++color) {
            const auto button=GetDlgItem(page,130+color);
            EXPECT((IsWindowEnabled(button)!=FALSE)==(presets[color]!=0));
            EXPECT(((GetWindowLongPtrW(button,GWL_STYLE)&WS_VISIBLE)!=0)==(presets[color]!=0));
        }
        const auto rgbWrites=memory->rgbWrites.load(), staticApplies=memory->staticColorApplies.load();
        for(int source=1;source<=4;++source)for(const int color:{0,1,4,6}) {
            {std::lock_guard lock(memory->mutex);memory->bytes[mc::ec::kFanControlRespective]=static_cast<std::uint8_t>(upper|source);}
            settings.refreshCommandAvailability();const auto button=GetDlgItem(page,130+color);
            EXPECT(button&&IsWindowEnabled(button));
            command(130+color);
            EXPECT(access.readStable(mc::ec::kFanControlRespective)==(upper|presets[color]));
            EXPECT((access.readStable(mc::ec::kKeyboardEffect)&mc::ec::kKeyboardRainbowBit)==0);
            EXPECT(GetPropW(button,L"MC.Selected")!=nullptr);
            EXPECT(memory->rgbWrites==rgbWrites&&memory->staticColorApplies==staticApplies);
            EXPECT(access.readStable(mc::ec::kKeyboardRed)==palette[0][0]);
            EXPECT(access.readStable(mc::ec::kKeyboardGreen)==palette[0][1]);
            EXPECT(access.readStable(mc::ec::kKeyboardBlue)==palette[0][2]);
            EXPECT(control.status().owned&&!control.status().error&&control.status().applied==baseline);
        }
        // Re-selecting Pulse keeps the selected preset; unsupported colour
        // commands cannot silently reset the effect or change RGB.
        command(121);
        EXPECT((access.readStable(mc::ec::kFanControlRespective)&7)==4);
        const auto beforeUnsupported=access.writeCount();
        for(const int color:{2,3,5,7})command(130+color);
        EXPECT(access.writeCount()==beforeUnsupported);
        EXPECT((access.readStable(mc::ec::kFanControlRespective)&7)==4);
        for(const int mode:{120,122})for(int color=0;color<8;++color) {
            command(mode);
            if (mode==122) {
                EXPECT(IsWindowEnabled(GetDlgItem(page,151))==FALSE);
                for(int index=0;index<8;++index) EXPECT(IsWindowEnabled(GetDlgItem(page,130+index))==FALSE);
                command(120); // Leave the firmware rainbow before choosing a static colour.
            }
            EXPECT(IsWindowEnabled(GetDlgItem(page,151))!=FALSE);
            const auto button=GetDlgItem(page,130+color);
            EXPECT(button&&IsWindowEnabled(button)&&(GetWindowLongPtrW(button,GWL_STYLE)&WS_VISIBLE));
            command(130+color);
            EXPECT(access.readStable(mc::ec::kFanControlRespective)==upper);
            EXPECT((access.readStable(mc::ec::kKeyboardEffect)&mc::ec::kKeyboardRainbowBit)==0);
            EXPECT(access.readStable(mc::ec::kKeyboardRed)==palette[color][0]);
            EXPECT(access.readStable(mc::ec::kKeyboardGreen)==palette[color][1]);
            EXPECT(access.readStable(mc::ec::kKeyboardBlue)==palette[color][2]);
            EXPECT(GetPropW(button,L"MC.Selected")!=nullptr);
            EXPECT(control.status().owned&&!control.status().error&&control.status().applied==baseline);
        }
        std::printf("Keyboard native breathing: 16 preset changes; static palette: 16 changes; fan-mode isolation checked\n");
    }
    // Keyboard defaults change only RGB/brightness, retaining Manual output.
    settings.refreshCommandAvailability();const HWND keyboardDefault=GetDlgItem(page,150);
    EXPECT(keyboardDefault&&IsWindowEnabled(keyboardDefault));
    const auto previousOwnerCount=memory->acquired.load();
    SendMessageW(page,WM_COMMAND,MAKEWPARAM(150,BN_CLICKED),reinterpret_cast<LPARAM>(keyboardDefault));
    EXPECT(wait([&]{return settings.trayState().ready&&!mc::deviceOperations().busy();}));
    EXPECT(control.status().owned&&!control.status().error&&control.status().applied==baseline);
    EXPECT(memory->acquired==previousOwnerCount&&(access.readStable(0x7C6)&4));
    EXPECT((access.readStable(mc::ec::kFanControlRespective)&mc::ec::kPulsationMask)==0);
    settings.refreshCommandAvailability();EXPECT(IsWindowEnabled(office));
    SendMessageW(page,WM_COMMAND,MAKEWPARAM(110,BN_CLICKED),reinterpret_cast<LPARAM>(office));
    EXPECT(wait([&]{return settings.trayState().ready;}));
    EXPECT(!control.status().error && control.status().owned && control.status().applied==baseline);
    const auto ownerCount=memory->acquired.load();
    for(const int preset:{1,2,0}) {
        auto result=control.performance(preset);
        EXPECT(result.wait_for(4s)==std::future_status::ready);
        try {static_cast<void>(result.get());}catch(const std::exception& e){std::printf("Performance error: %s\n",e.what());++failures;}
        EXPECT(wait([&]{return !mc::deviceOperations().busy();}));
        const auto state=control.status();
        EXPECT(!state.error && state.owned && state.applied==baseline && state.controlled[0] && state.controlled[1]);
        std::lock_guard lock(memory->mutex);
        EXPECT(memory->bytes[0x751]==(preset==0?0xA0:preset==1?0:0x10));
        EXPECT((memory->bytes[0x726]&0x80) && (memory->bytes[0x727]&0x40) && (memory->bytes[0x7C6]&4));
        EXPECT(memory->acquired.load()==ownerCount);
    }
    auto uiOperation=mc::deviceOperations().tryBegin();EXPECT(uiOperation!=nullptr);
    auto coordinated=control.performance(1,uiOperation);
    EXPECT(coordinated.wait_for(4s)==std::future_status::ready);static_cast<void>(coordinated.get());
    EXPECT(mc::deviceOperations().busy());uiOperation.reset();EXPECT(wait([]{return !mc::deviceOperations().busy();}));
    memory->failRestore=true;
    auto failedStop=control.nativeAndWait();EXPECT(failedStop.wait_for(4s)==std::future_status::ready);
    bool failed=false;try{failedStop.get();}catch(const std::exception&){failed=true;}EXPECT(failed);
    EXPECT(wait([&]{return control.status().needsRestore && !mc::deviceOperations().busy();}));
    settings.refreshCommandAvailability();EXPECT(!IsWindowEnabled(office));EXPECT(!settings.trayState().performanceReady);
    bool guarded=false;try{static_cast<void>(control.performance(2));}catch(const std::exception&){guarded=true;}EXPECT(guarded);
    memory->failRestore=false;
    auto stop=control.nativeAndWait();EXPECT(stop.wait_for(4s)==std::future_status::ready);stop.get();
    EXPECT(wait([]{return !mc::deviceOperations().busy();}));
    EXPECT(!control.status().error && !control.status().owned);
    EXPECT((access.readStable(mc::ec::kAppPresence)&1)!=0);
    settings.refreshCommandAvailability();EXPECT(!IsWindowEnabled(office)&&IsWindowEnabled(stockOffice));EXPECT(settings.trayState().performanceReady);
    {std::lock_guard lock(memory->mutex);EXPECT(memory->bytes[0x751]==0);}
    std::printf("Shared owner / switching guard / Manual preservation / native transition: %d failure(s); simulated EC only\n",failures);
    return failures?1:0;
}
int main(int argc,char** argv) {
    try{return runTests(argc,argv);}
    catch(const std::exception& error){std::fprintf(stderr,"Unhandled test exception [%s]: %s\n",testStage,error.what());return 1;}
    catch(...){std::fprintf(stderr,"Unhandled non-standard test exception [%s]\n",testStage);return 1;}
}
