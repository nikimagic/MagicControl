#include "MicrophoneKeysWindows.h"
#include "KeysHardwareWindows.h"
#include "UniwillControlOwnerWindows.h"
#include "MagicControl/MicrophoneControl.h"
#include "MagicControl/QuickSwitch.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/Logging.h"
#include <stdexcept>
namespace mc::windows {
namespace {
bool indicatorAvailable(const KeysHardwareContext& c){
    return !c.preview&&uniwillMicrophoneLedProfile(c.profile)&&c.ec&&c.ec->boardVerified()&&c.ec->info().writable&&!c.ec->observationsPaused();
}
std::optional<bool> indicator(const KeysHardwareContext& c){
    if(!indicatorAvailable(c))return {};
    try{return readMicrophoneMuteLed(*c.ec);}catch(...){return {};}
}
}
DeviceState readMicrophoneKey(const KeysHardwareContext& c){
    const auto actual=readDefaultMicrophone();DeviceState result{actual.enabled,actual.available,actual.detail};
    if(uniwillMicrophoneLedProfile(c.profile)&&!c.preview){
        const auto led=indicator(c);
        result.detail+=" · EC mute LED flag="+std::string(led?(*led?"1":"0"):"unavailable");
        if(led&&actual.enabled&&*led==*actual.enabled)result.detail+=" (not synchronized)";
    }
    return result;
}
KeyObservation observeMicrophoneKey(const KeysHardwareContext& c){
    const auto actual=readDefaultMicrophone();const auto led=actual.available&&actual.enabled?indicator(c):std::nullopt;
    return {actual.enabled,true,actual.available,actual.detail,led&&actual.enabled&&*led==*actual.enabled};
}
void applyMicrophoneKey(const KeysHardwareContext& c,bool enabled,bool observeOnly){
    if(!observeOnly)setDefaultMicrophoneEnabled(enabled);
    const auto actual=readDefaultMicrophone();
    if(!actual.available||!actual.enabled||(!observeOnly&&*actual.enabled!=enabled))
        throw std::runtime_error("ОС не подтвердила состояние микрофона");
    if(!indicatorAvailable(c))return; // System microphone works even without EC.
    try{
        if(!uniwillControlOwner().empty())throw std::runtime_error("Microphone LED is owned by OEM");
        // Re-read actual Windows state immediately before mirroring it. An
        // external event never replays a stale requested mute/unmute value.
        const auto current=readDefaultMicrophone();
        if(!current.available||!current.enabled)throw std::runtime_error("Microphone unavailable before LED sync");
        if(!observeOnly&&current.enabled!=std::optional<bool>(enabled))throw std::runtime_error("Microphone changed before LED sync");
        setMicrophoneMuteLed(*c.ec,!*current.enabled);
    }catch(const std::exception& error){
        // Never unmute or undo a confirmed Windows action because optional
        // indication failed. Its getter reports the independent LED state.
        logMessage("keys","Microphone confirmed by Windows; LED unconfirmed: "+std::string(error.what()));
    }
}
}
