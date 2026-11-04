#include <windows.h>
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/Logging.h"
#include "MagicControl/PortableSettings.h"
#include "../src/gui/windows/FansPage.h"
#include "../src/platform/windows/FanControlWindows.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
using namespace std::chrono_literals;
namespace {
int failures{};
#define EXPECT(value) do {if(!(value)){++failures;std::printf("FAIL line %d: %s\n",__LINE__,#value);}}while(false)
struct Memory {
    std::recursive_mutex mutex;
    std::array<std::uint8_t,65536> bytes{};
    std::atomic<bool> holdFeedback{};
    std::atomic<unsigned> delayNextTemperatureMs{};
    Memory() {
        bytes[0x454]=82;bytes[0x456]=72;bytes[0x740]=26;bytes[0x7C0]=0;
        bytes[mc::ec::kRamTableCapability]=mc::ec::kRamTableSupportedBit;
        bytes[0x751]=0xA0;bytes[mc::ec::kCpuRpmHigh]=9;bytes[mc::ec::kCpuRpmLow]=196;
        bytes[mc::ec::kGpuRpmHigh]=7;bytes[mc::ec::kGpuRpmLow]=208;
    }
};
class Backend final:public mc::IEcBackend {
    std::shared_ptr<Memory> m_;mc::BackendInfo info_;
public:
    explicit Backend(std::shared_ptr<Memory> m):m_(std::move(m)){info_.id="windows-acpi-driver";info_.writable=true;info_.maxAddress=0xFFFF;}
    const mc::BackendInfo& info()const noexcept override{return info_;}
    std::uint8_t read(std::uint16_t address)override{std::lock_guard lock(m_->mutex);return m_->bytes[address];}
    void write(std::uint16_t address,std::uint8_t value)override{
        std::lock_guard lock(m_->mutex);m_->bytes[address]=value;
        if(!m_->holdFeedback){if(address==0xF20)m_->bytes[mc::ec::kCpuPwm]=value;if(address==0xF50)m_->bytes[mc::ec::kGpuPwm]=value;}
        if(address==0x751&&(value&0x40))m_->bytes[mc::ec::kCpuPwm]=m_->bytes[mc::ec::kGpuPwm]=200;
    }
    void lockTransaction()override{m_->mutex.lock();}
    void unlockTransaction()noexcept override{m_->mutex.unlock();}
};
struct Owner final:mc::windows::FanOwnership {void restoreService()override{}};
mc::MonitoringSnapshot temperatures(){mc::MonitoringSnapshot s;s.processorSampledAtMs=GetTickCount64();s.processors[0].celsius=45;s.processors[1].celsius=40;return s;}
struct Environment final:mc::windows::FanControlEnvironment {
    std::shared_ptr<Memory> m;
    explicit Environment(std::shared_ptr<Memory> memory):m(std::move(memory)){}
    mc::MonitoringSnapshot readTemperatures()override{if(const auto delay=m->delayNextTemperatureMs.exchange(0))std::this_thread::sleep_for(std::chrono::milliseconds(delay));return temperatures();}
    std::unique_ptr<mc::windows::FanOwnership> acquireOwnership(bool)override{return std::make_unique<Owner>();}
};
template<class Check>bool wait(Check check,std::chrono::milliseconds timeout=5s){
    const auto end=std::chrono::steady_clock::now()+timeout;
    while(std::chrono::steady_clock::now()<end){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(check())return true;std::this_thread::sleep_for(5ms);}return check();
}
struct SavedFile {
    std::filesystem::path path;std::string contents;bool existed;
    explicit SavedFile(std::filesystem::path value):path(std::move(value)),existed(std::filesystem::exists(path)){if(existed){std::ifstream f(path,std::ios::binary);contents.assign(std::istreambuf_iterator<char>(f),{});}}
    ~SavedFile(){if(existed){std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<contents;}else{std::error_code e;std::filesystem::remove(path,e);}}
};
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);mc::setLoggingEnabled(false);
    SavedFile saved(mc::portableSettingsPath("fan-curves.csv"));
    mc::FanCurveProfiles profiles;profiles.linked=true;profiles.selected.fill(mc::FanCurvePreset::Default);
    mc::FanCurveTable manual{},minimum{};manual[0]={30,15};manual[1]={40,55};manual[2]={60,100};minimum[0]={30,7};minimum[1]={40,20};minimum[2]={60,100};
    for(int channel=0;channel<2;++channel){profiles.curves[0][channel]=manual;profiles.curves[3][channel]=minimum;}
    std::string error;EXPECT(mc::saveFanCurveProfiles(saved.path,profiles,error));
    auto memory=std::make_shared<Memory>();mc::EcAccess ec(std::make_unique<Backend>(memory));
    const auto& identity=ec.verifyBoard();
    if(!identity.verified){std::printf("Fake board verification failed: %s\n",identity.error.c_str());return 1;}
    auto controller=std::make_shared<mc::windows::FanController>(ec,std::make_unique<Environment>(memory));
    const HWND parent=CreateWindowExW(0,L"STATIC",L"FANs buttons test",0,0,0,0,0,HWND_MESSAGE,nullptr,nullptr,nullptr);if(!parent)return 10;
    {
        mc::gui::FansPage fans(parent,&ec,nullptr,{},controller,temperatures);
        fans.arrange(RECT{0,0,1600,1200},192,true);
        const HWND page=FindWindowExW(parent,nullptr,L"MagicControl.FansPage",nullptr);
        const HWND view=FindWindowExW(page,nullptr,L"STATIC",nullptr);
        const auto button=[&](int channel,int preset){return GetDlgItem(view,320+channel*4+preset);};
        EXPECT(wait([&]{fans.refreshCommandAvailability();return button(0,0)&&IsWindowEnabled(button(0,0));}));
        const auto click=[&](int channel,int preset){
            fans.refreshCommandAvailability();const HWND target=button(channel,preset);EXPECT(target&&IsWindowEnabled(target));
            const auto revision=controller->status().revision;
            SendMessageW(view,WM_COMMAND,MAKEWPARAM(320+channel*4+preset,BN_CLICKED),reinterpret_cast<LPARAM>(target));
            EXPECT(wait([&]{
                const auto s=controller->status();
                if(s.revision<=revision||s.applying||mc::deviceOperations().busy())return false;
                fans.refreshCommandAvailability();
                for(int c=0;c<2;++c)for(int p=0;p<4;++p)if(!IsWindowEnabled(button(c,p)))return false;
                return true;
            }));
            const auto s=controller->status();EXPECT(!s.error);
            if(preset==0){EXPECT(!s.owned&&!s.maximum&&(ec.readStable(0x7C6)&4)==0&&(ec.readStable(0x751)&0x40)==0);}
            else if(preset==3){EXPECT(s.maximum&&s.controlled[0]&&s.controlled[1]&&(ec.readStable(0x751)&0x40));}
            else{const auto& expected=preset==1?manual:minimum;EXPECT(s.owned&&!s.maximum&&s.applied[0]==expected&&s.applied[1]==expected&&(ec.readStable(0x7C6)&4));}
            for(int c=0;c<2;++c)for(int p=0;p<4;++p)EXPECT(IsWindowEnabled(button(c,p)));
        };
        // Real WM_COMMAND handlers: all 16 ordered pairs on both CPU/GPU rows.
        for(int channel=0;channel<2;++channel)for(int from=0;from<4;++from)for(int to=0;to<4;++to){click(channel,from);click(channel,to);}
        click(0,3);memory->holdFeedback=true;memory->delayNextTemperatureMs=11000;
        const auto revision=controller->status().revision;
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(322,BN_CLICKED),reinterpret_cast<LPARAM>(button(0,2)));
        EXPECT(wait([&]{fans.refreshCommandAvailability();auto s=controller->status();return s.revision>revision&&!s.applying&&!mc::deviceOperations().busy();},16s));
        auto s=controller->status();
        // Confirmation describes EC mode flags, not physical PWM execution.
        EXPECT(!s.maximum&&!s.error&&s.target[0]==20&&s.confirmed[0]);
        EXPECT(ec.readStable(mc::ec::kCpuPwm)==200);
        std::this_thread::sleep_for(1300ms);s=controller->status();EXPECT(!s.maximum&&s.target[0]==20);
        // Default is an available exit while actual PWM is still at the old Max.
        click(0,0);memory->holdFeedback=false;
        // A genuine power event during an unconfirmed Min must not replay Max.
        click(0,3);memory->holdFeedback=true;click(0,2);
        controller->suspendPower();controller->resumePower();
        EXPECT(wait([&]{fans.refreshCommandAvailability();return !mc::deviceOperations().busy()&&!controller->status().applying;}));
        // The selected manual curve may retain ownership while recovering.
        EXPECT(!controller->status().maximum);
        EXPECT((ec.readStable(0x751)&0x40)==0);click(0,0);memory->holdFeedback=false;
    }
    controller.reset();DestroyWindow(parent);
    std::printf("FANs Default/Manual/Min/Max: 32 transition pairs, slow EC >10s, delayed PWM, Default escape: %d failure(s); fake EC/sensors only\n",failures);
    return failures?1:0;
}
