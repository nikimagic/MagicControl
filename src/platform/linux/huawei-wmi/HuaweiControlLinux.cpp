#include "MagicControl/ControlAudit.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include "../../../protocols/huawei/BatteryThresholds.h"
#include "../../../protocols/huawei/KeyboardTimeout.h"
#include "HlylOperationGate.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <thread>
#include <chrono>

namespace mc {
namespace {
namespace fs=std::filesystem;
// Reentrant for one typed charge transaction spanning read/decision/write;
// all other HLYL calls still share this same in-process gate.
linux_platform::HlylOperationGate gate;
#ifdef MAGICCONTROL_HLYL_SYSFS_TEST
fs::path testRoot,platformRoot,keyboardRoot,powerSupplyRoot;bool testBoardAllowed{};
bool insideTestRoot(const fs::path& path){
    if(testRoot.empty())return false;std::error_code error;const auto resolved=fs::weakly_canonical(path,error);if(error)return false;
    auto expected=testRoot.begin(),actual=resolved.begin();
    for(;expected!=testRoot.end();++expected,++actual)if(actual==resolved.end()||*actual!=*expected)return false;
    return actual!=resolved.end();
}
void requireBoard(){if(!testBoardAllowed)throw std::runtime_error("HLYL sysfs test: board rejected");}
std::string text(const fs::path& path){if(!insideTestRoot(path))throw std::runtime_error("HLYL sysfs test: read outside fixture");std::ifstream f(path);std::string s;std::getline(f,s);return s;}
void put(const fs::path& path,const std::string& value){if(!insideTestRoot(path)||!fs::is_regular_file(path))throw std::runtime_error("HLYL sysfs test: write outside fixture");std::ofstream out(path);out<<value<<'\n';out.flush();if(!out)throw std::runtime_error("HLYL sysfs test: write failed");}
#else
const fs::path platformRoot="/sys/devices/platform/huawei-wmi";
const fs::path keyboardRoot="/sys/class/leds/huawei::kbd_backlight";
const fs::path powerSupplyRoot="/sys/class/power_supply";
#ifdef MAGICCONTROL_HLYL_LOCK_TEST
// Test-only build: no real board, sysfs read or sysfs write is allowed.
void requireBoard(){throw std::runtime_error("HLYL lock test: hardware disabled");}
std::string text(const fs::path&){throw std::runtime_error("HLYL lock test: sysfs read disabled");}
void put(const fs::path&,const std::string&){throw std::runtime_error("HLYL lock test: sysfs write disabled");}
#else
void requireBoard(){if(deviceProfile(readBoardDescriptor()).id!="huawei-hlyl-wxx9")throw std::runtime_error("Huawei: плата не соответствует HLYL-WXX9");}
std::string text(const fs::path& path){std::ifstream f(path);std::string s;std::getline(f,s);if(audit::current())audit::event("sysfs_read_response","\"path\":"+audit::quote(path.string())+",\"raw\":"+audit::quote(s)+",\"opened\":"+(f.is_open()?"true":"false"));return s;}
void put(const fs::path& path,const std::string& value){audit::Scope trace("huawei.sysfs_write","\"path\":"+audit::quote(path.string())+",\"requested\":"+audit::quote(value));std::ofstream out(path);out<<value<<'\n';out.flush();if(!out)throw std::runtime_error("Huawei sysfs: write rejected path="+path.string()+" value="+value);trace.finish("write_returned_readback_pending");}
#endif
#endif
int keyboardTimeout(const fs::path& path){
    const auto raw=text(path);int value=-1;std::istringstream row(raw);row>>value;row>>std::ws;
    if(raw.empty()||!row.eof()||value<0||value>3600)throw std::runtime_error("Huawei sysfs timeout unavailable/invalid path="+path.string()+" raw="+raw);
    return value;
}
struct ThresholdPaths {fs::path pair,start,end;};
ThresholdPaths thresholdPaths(){
    if(fs::exists(platformRoot/"charge_control_thresholds"))return {platformRoot/"charge_control_thresholds",{}, {}};
    std::error_code error;
    for(const auto& entry:fs::directory_iterator(powerSupplyRoot,error)){
        if(text(entry.path()/"type")!="Battery")continue;
        const auto start=entry.path()/"charge_control_start_threshold",end=entry.path()/"charge_control_end_threshold";
        if(fs::exists(start)&&fs::exists(end))return {{},start,end};
    }
    return {};
}
std::pair<int,int> thresholds(const ThresholdPaths& paths){
    int start=-1,end=-1;if(!paths.pair.empty()){std::istringstream row(text(paths.pair));row>>start>>end;}
    else if(!paths.start.empty()){std::istringstream(text(paths.start))>>start;std::istringstream(text(paths.end))>>end;}
    if(!huawei::validChargeThresholds({start,end}))throw std::runtime_error("Huawei sysfs: invalid charge pair="+huawei::chargeThresholdText({start,end})+" path="+(!paths.pair.empty()?paths.pair:paths.start).string());
    return {start,end};
}
huawei::ChargeThresholds stableThresholds(const ThresholdPaths& paths,std::optional<huawei::ChargeThresholds> expected={}){
    return huawei::stableChargeThresholds([&]{
        try{return thresholds(paths);}
        catch(const std::exception& error){logMessage("huawei_sysfs",std::string("charge getter retry observation: ")+error.what());throw;}
    },[]{std::this_thread::sleep_for(std::chrono::milliseconds(150));},expected);
}
void writeThresholds(const ThresholdPaths& paths,huawei::ChargeThresholds target,huawei::ChargeThresholds& expected){
    const auto verify=[&]{const auto current=thresholds(paths);if(current!=expected)throw std::runtime_error("Huawei sysfs: charge ownership changed; current="+huawei::chargeThresholdText(current));};
    if(!paths.pair.empty()){
        verify();expected=target;put(paths.pair,std::to_string(target.first)+" "+std::to_string(target.second));return;
    }
    const auto first=[&]{verify();expected.first=target.first;put(paths.start,std::to_string(target.first));};
    const auto last=[&]{verify();expected.second=target.second;put(paths.end,std::to_string(target.second));};
    // Also valid when entering/leaving native disabled0/0. Track each attempted
    // component before the write: a failing sysfs call may already have applied.
    if(target.first>expected.second){last();first();}else{first();last();}
}
class HlylControl final:public LaptopControl {
    std::string lastLog;
public:
    std::string_view chargePolicyId() const noexcept override{return "huawei-hlyl-wxx9";}
    void withChargePolicyTransaction(const std::function<void()>& action) override{
        std::lock_guard lock(gate);action();
    }
    LaptopControlSnapshot read() override {
        std::lock_guard lock(gate);LaptopControlSnapshot state;state.source="Linux · huawei-wmi";std::string issues;
        const auto unavailable=[&](const std::string& message){if(!issues.empty())issues+="; ";issues+=message;};
        try {
            requireBoard();state.connected=fs::exists(platformRoot);const auto paths=thresholdPaths();
            try {const auto [start,end]=stableThresholds(paths);state.chargeStart=start;state.chargeEnd=end;state.connected=true;
                state.chargeWritable=!paths.pair.empty()?access(paths.pair.c_str(),W_OK)==0:access(paths.start.c_str(),W_OK)==0&&access(paths.end.c_str(),W_OK)==0;
            }catch(const std::exception& error){unavailable(error.what());}
            const auto fn=text(platformRoot/"fn_lock_state");if(fn=="0"||fn=="1"){state.fnLock=fn=="1";state.fnWritable=access((platformRoot/"fn_lock_state").c_str(),W_OK)==0;state.connected=true;}
            else unavailable("sysfs getter fn_lock_state: absent/unreadable/invalid raw="+fn);
            // Only the OEM LED node; never attach to screen brightness or to
            // another keyboard. Do not install modules/chmod sysfs here.
            if(text(keyboardRoot/"max_brightness")=="2"){
                const auto value=text(keyboardRoot/"brightness");
                if(value=="0"||value=="1"||value=="2"){state.keyboardLevel=std::stoi(value);state.keyboardWritable=access((keyboardRoot/"brightness").c_str(),W_OK)==0;}
                else unavailable("sysfs brightness: absent/unreadable/invalid raw="+value);
            }else unavailable("sysfs huawei::kbd_backlight: max_brightness2 not provided");
            try{state.keyboardTimeoutSeconds=keyboardTimeout(platformRoot/"kbdlight_timeout");state.keyboardTimeoutWritable=access((platformRoot/"kbdlight_timeout").c_str(),W_OK)==0;}
            catch(const std::exception& error){unavailable(error.what());}
            state.status=state.connected?"Huawei sysfs · возможности зависят от драйвера ядра и прав доступа":"Huawei: интерфейс huawei-wmi не найден в sysfs";
        }catch(const std::exception& e){state.status=e.what();}
        if(!issues.empty())state.status+="; "+issues;
        if(state.chargeStart==0&&state.chargeEnd==0)state.status+="; native charge protection disabled0/0, not100%";
        if(state.status!=lastLog){lastLog=state.status;logMessage("huawei_sysfs",state.status);}return state;
    }
    void setChargeThresholds(int start,int end) override {
        std::lock_guard lock(gate);requireBoard();if(start<0||end<1||start>end||end>100)throw std::invalid_argument("Huawei: неверные пороги зарядки");
        if(start==0&&end==100)start=95; // 100% travel, not disable/Smart Charge.
        const auto paths=thresholdPaths();const auto before=stableThresholds(paths);const huawei::ChargeThresholds target{start,end};if(before==target)return;
        auto attempted=before;
        try{writeThresholds(paths,target,attempted);stableThresholds(paths,target);logMessage("huawei_sysfs","Charge confirmed="+huawei::chargeThresholdText(target));}
        catch(const std::exception& failure){
            const std::string reason=failure.what();std::string rollback;
            try{
                auto current=stableThresholds(paths);
                if(!huawei::chargeThresholdsOwned(current,before,attempted))throw std::runtime_error("ownership lost; current="+huawei::chargeThresholdText(current));
                if(current!=before)writeThresholds(paths,before,current);
                stableThresholds(paths,before);rollback="прежние пороги восстановлены и подтверждены";
            }catch(const std::exception& error){rollback=std::string("откат не подтверждён; состояние неизвестно: ")+error.what();}
            logMessage("huawei_sysfs",reason+"; "+rollback);throw std::runtime_error(reason+"; "+rollback);
        }
    }
    void setFnLock(bool enabled) override {
        std::lock_guard lock(gate);requireBoard();const auto path=platformRoot/"fn_lock_state";const auto before=text(path);
        if(before!="0"&&before!="1")throw std::runtime_error("Huawei: Fn Lock недоступен");
        if((before=="1")==enabled)return;
        put(path,enabled?"1":"0");if(text(path)!=(enabled?"1":"0"))throw std::runtime_error("Huawei: Fn Lock не подтверждён чтением");
    }
    void setKeyboardLevel(int level) override {
        std::lock_guard lock(gate);requireBoard();
        if(level<0||level>2||text(keyboardRoot/"max_brightness")!="2")throw std::runtime_error("Huawei: трёхуровневая подсветка недоступна");
        const auto before=text(keyboardRoot/"brightness");if(before!="0"&&before!="1"&&before!="2")throw std::runtime_error("Huawei: яркость не распознана");
        put(keyboardRoot/"brightness",std::to_string(level));
        if(text(keyboardRoot/"brightness")!=std::to_string(level))throw std::runtime_error("Huawei: яркость не подтверждена чтением");
    }
    void setKeyboardTimeout(int seconds) override {
        std::lock_guard lock(gate);requireBoard();if(seconds<0||seconds>3600)throw std::invalid_argument("Huawei: таймаут 0–3600 секунд");
        const auto path=platformRoot/"kbdlight_timeout";
        const auto actual=huawei::applyKeyboardTimeout(seconds,[&]{return keyboardTimeout(path);},[&](int value){put(path,std::to_string(value));},
            []{std::this_thread::sleep_for(std::chrono::milliseconds(100));},[](const std::string& message){logMessage("huawei_sysfs",message);});
        logMessage("huawei_sysfs","Keyboard timeout confirmed="+std::to_string(actual.after)+" seconds; wrote="+std::to_string(actual.wrote));
    }
};
}
#ifdef MAGICCONTROL_HLYL_SYSFS_TEST
namespace linux_platform {
std::unique_ptr<LaptopControl> createHlylSysfsTestControl(const fs::path& fixture,bool admitted){
    const auto base=fs::canonical(fs::path(MAGICCONTROL_TEST_ROOT));const auto root=fs::canonical(fixture);
    auto expected=base.begin(),actual=root.begin();
    for(;expected!=base.end();++expected,++actual)if(actual==root.end()||*actual!=*expected)throw std::runtime_error("HLYL sysfs test: fixture outside build root");
    if(actual==root.end())throw std::runtime_error("HLYL sysfs test: build root is not a fixture");
    std::lock_guard lock(gate);testRoot=root;platformRoot=root/"platform";keyboardRoot=root/"keyboard";
    powerSupplyRoot=root/"supplies";testBoardAllowed=admitted;
    return auditedLaptopControl(std::make_unique<HlylControl>());
}
}
#elif defined(MAGICCONTROL_HLYL_LOCK_TEST)
namespace linux_platform {
std::unique_ptr<LaptopControl> createHlylLockTestControl(){return auditedLaptopControl(std::make_unique<HlylControl>());}
}
#else
std::unique_ptr<LaptopControl> createLaptopControl(){if(deviceProfile(readBoardDescriptor()).id!="huawei-hlyl-wxx9")return {};return auditedLaptopControl(std::make_unique<HlylControl>());}
#endif
}
