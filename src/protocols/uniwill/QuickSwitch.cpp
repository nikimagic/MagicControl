#include "MagicControl/QuickSwitch.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/ActionLog.h"
#include <chrono>
#include <stdexcept>
#include <thread>
#include <mutex>

namespace mc {
namespace {
struct Field {std::uint16_t address;std::uint8_t mask;};
Field field(QuickSwitch key){
    // Independently recovered from each admitted OEM MySettingManager:
    // GM6 active5.56, GM5 5.52.49.6, L16 5.60.60.17, HYDROC5.23.51.56.
    // AC here is the documented EC fallback; BIOS-owned AC policy is a separate
    // setting, not proof of a physical AC-start until checked after a reboot.
    switch(key){
    case QuickSwitch::Windows:return {0x768,0x01};
    case QuickSwitch::Fn:return {0x74E,0x10};
    case QuickSwitch::Usb:return {0x767,0x10};
    case QuickSwitch::Ac:return {0x726,0x08};
    }
    throw std::invalid_argument("Unknown quick switch");
}
void admit(EcAccess& ec){
    if(!ec.boardVerified()||!uniwillQuickSwitchProfile(deviceProfile(ec.verifyBoard().board).id))
        throw BoardNotVerified("Quick switches: exact Uniwill board not verified");
}
}
bool readQuickSwitch(EcAccess& ec,QuickSwitch key){
    admit(ec);const auto f=field(key);const EcAccess::Transaction transaction(ec);
    return (ec.readStable(f.address)&f.mask)!=0;
}
void setQuickSwitch(EcAccess& ec,QuickSwitch key,bool enabled){
    audit::Scope trace("keys.uniwill_switch","\"switch\":"+std::to_string(static_cast<int>(key))+",\"enabled\":"+(enabled?"true":"false"));
    admit(ec);ec.ensureWritable();
    // Serialize Win Lock requests, including their settling interval. A toggle
    // is never retried after an ambiguous response: that could undo a late ACK.
    static std::mutex windowsGate;
    static std::chrono::steady_clock::time_point lastGm6Trigger{};
    std::unique_lock winLock(windowsGate,std::defer_lock);
    const bool gm6=deviceProfile(ec.verifyBoard().board).id=="mechrevo-gm6ir0c";
    if(key==QuickSwitch::Windows){
        winLock.lock();
        if(gm6&&lastGm6Trigger.time_since_epoch().count()){
            audit::event("winlock_settle_wait","\"minimum_trigger_interval_ms\":2000");
            std::this_thread::sleep_until(lastGm6Trigger+std::chrono::seconds(2));
        }
    }
    const EcAccess::Transaction transaction(ec);
    const auto f=field(key);
    if(readQuickSwitch(ec,key)!=enabled){
        if(key==QuickSwitch::Windows){
            // 767 contains USB/RGB/lightbar fields. Send bit0 ONCE and verify
            // 768.0, not whether the command byte has already cleared itself.
            ec.updateBitsCommand(0x767,0x01,0x01);
            if(gm6)lastGm6Trigger=std::chrono::steady_clock::now();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(gm6?3:1);
            do{
                if(readQuickSwitch(ec,key)==enabled){trace.finish("readback_confirmed");return;}
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }while(std::chrono::steady_clock::now()<deadline);
            throw std::runtime_error("EC не подтвердил блокировку Windows");
        }
        (void)ec.updateBitsVerified(f.address,f.mask,enabled?f.mask:0);
    }
    if(readQuickSwitch(ec,key)!=enabled)throw std::runtime_error("EC не подтвердил переключение");
    trace.finish("readback_confirmed");
}
bool readCopilotKey(EcAccess& ec){
    admit(ec);
    if(!uniwillCopilotEcProfile(deviceProfile(ec.verifyBoard().board).id))throw std::runtime_error("No verified EC Copilot ABI");
    const EcAccess::Transaction transaction(ec);
    // Wujie firmware C47F checks both AP_OEM_BYTEB.2 and application presence.
    return (ec.readStable(0x728)&4)!=0&&(ec.readStable(0x741)&1)!=0;
}
namespace {
void admitMicrophoneLed(EcAccess& ec){
    admit(ec);
    if(!uniwillMicrophoneLedProfile(deviceProfile(ec.verifyBoard().board).id)||
       ec.readStable(ec::kBoardEcVersion)!=82||ec.readStable(ec::kBoardSystemId)!=72||ec.readStable(ec::kBoardProjectId)!=26||ec.readStable(0x6e6)!=1)
        throw BoardNotVerified("Microphone LED: exact Wujie identity not confirmed");
}
}
bool readMicrophoneMuteLed(EcAccess& ec){
    const EcAccess::Transaction transaction(ec);admitMicrophoneLed(ec);
    return (ec.readStable(0x7a6)&4)!=0;
}
void setMicrophoneMuteLed(EcAccess& ec,bool muted){
    audit::Scope trace("keys.uniwill_microphone_led","\"requested_muted\":"+std::string(muted?"true":"false"));
    const EcAccess::Transaction transaction(ec);admitMicrophoneLed(ec);ec.ensureWritable();
    // Preserve battery protection7A6[5:4], touchpad LED.3, hotkey gate.6
    // and all other fields. No APP/FAN/RAM/PL or capture-device command here.
    if(((ec.readStable(0x7a6)&4)!=0)!=muted)ec.updateBitsVerified(0x7a6,4,muted?4:0);
    if(readMicrophoneMuteLed(ec)!=muted)throw std::runtime_error("EC microphone LED readback mismatch");
    trace.finish("readback_confirmed");
}
void setCopilotKey(EcAccess& ec,bool locked){
    audit::Scope trace("keys.uniwill_copilot","\"enabled\":"+std::string(locked?"true":"false"));
    admit(ec);ec.ensureWritable();
    if(!uniwillCopilotEcProfile(deviceProfile(ec.verifyBoard().board).id))throw std::runtime_error("No verified EC Copilot ABI");
    const EcAccess::Transaction transaction(ec);
    // OEM service holds AP presence. This shared bit alone selects no fan mode;
    // do not clear it on Off and never change FAN/RAM/PL or other byte bits.
    if(locked&&!(ec.readStable(0x741)&1))ec.updateBitsVerified(0x741,1,1);
    const auto before=ec.readStable(0x728);
    if(((before&4)!=0)!=locked)ec.updateBitsVerified(0x728,4,locked?4:0);
    if(readCopilotKey(ec)!=locked)throw std::runtime_error("EC Copilot readback mismatch");
    trace.finish("readback_confirmed");
}
}
