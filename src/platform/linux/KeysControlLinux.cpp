#include "KeysControlLinux.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/MicrophoneControl.h"
#include "MagicControl/KeysBackendPolicy.h"
#include "MagicControl/KeyResourceControl.h"
#include <stdexcept>
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/Logging.h"
#include "MagicControl/ControlAudit.h"

namespace mc::linux_platform {
namespace {
struct KeyRequest {KeyAction action;bool on;};
struct KeyContext {std::string_view profile;bool preview;LaptopControl* control;std::mutex& gate;};
using KeyRoutes=ResourceControlTable<KeyRequest,KeyContext,void>;
void applyMicrophone(const KeyRequest& request,const KeyContext& context){
    if(keysBackend(context.profile,request.action,true,context.preview)!=KeysBackend::System)
        throw std::runtime_error("Для этой функции нет подтверждённого Linux-адаптера.");
    setDefaultMicrophoneEnabled(request.on);
}
void applyFn(const KeyRequest& request,const KeyContext& context){
    if(keysBackend(context.profile,request.action,true,context.preview)!=KeysBackend::HuaweiHwmi||!context.control)
        throw std::runtime_error("Для этой функции нет подтверждённого Linux-адаптера.");
    std::lock_guard lock(context.gate);context.control->setFnLock(request.on);
}
const KeyRoutes& keyRoutes(){
    static const auto routes=[] {
        const auto mic=keyResourceControlKey(KeyAction::Microphone),fn=keyResourceControlKey(KeyAction::Fn);
        const std::array<KeyRoutes::Binding,2> bindings{{
            {mic.kind,mic.instance,mic.operation,applyMicrophone},
            {fn.kind,fn.instance,fn.operation,applyFn}}};
        return KeyRoutes(bindings);
    }();
    return routes;
}
}
KeyObservation observeKeyForRestore(std::string_view profile,bool preview,LaptopControl* control,std::mutex& gate,KeyAction key) {
    const auto route=keysBackend(profile,key,true,preview,keyActionVisible(profile,key));
    if(route==KeysBackend::Unavailable)return {{},false,false,"No Linux adapter"};
    if(key==KeyAction::Microphone){const auto s=readDefaultMicrophone();return {s.enabled,true,s.available,s.detail};}
    if(key==KeyAction::Fn&&control){std::lock_guard lock(gate);const auto s=control->read();return {s.fnLock,true,s.fnWritable,s.status};}
    return {{},false,false,"No Linux adapter"};
}
void applyKey(std::string_view profile,bool preview,LaptopControl* control,std::mutex& gate,KeyAction key,bool on) {
    if(!keyActionVisible(profile,key))throw std::runtime_error("Для этого устройства функция отсутствует.");
    const auto resourceKey=keyResourceControlKey(key);
    if(!keyRoutes().hasRoute(resourceKey))throw std::runtime_error("Для этой функции нет подтверждённого Linux-адаптера.");
    keyRoutes().execute(resourceKey,KeyRequest{key,on},KeyContext{profile,preview,control,gate});
}
void readKeyState(LaptopControl* control,std::mutex& gate,LaptopControlSnapshot& snapshot,MicrophoneState& microphone) {
    try{if(control){std::lock_guard lock(gate);snapshot=control->read();}}
    catch(const std::exception& e){snapshot.status=e.what();}
    microphone=readDefaultMicrophone();
}
void confirmKey(KeyAction key,bool on,const LaptopControlSnapshot& snapshot,const MicrophoneState& microphone,std::string& error) {
    if(!error.empty())return;
    const KeyObservation actual=key==KeyAction::Microphone?
        KeyObservation{microphone.enabled,true,microphone.available,microphone.detail}:
        KeyObservation{snapshot.fnLock,true,snapshot.fnWritable,snapshot.status};
    completeKeyResource(key,on,actual,error);
}
void executeKeyOperation(std::string_view profile,bool preview,LaptopControl* control,std::mutex& gate,
                         std::optional<std::pair<KeyAction,bool>> request,
                         LaptopControlSnapshot& snapshot,MicrophoneState& microphone,std::string& error) {
    try{if(request)applyKey(profile,preview,control,gate,request->first,request->second);}
    catch(const std::exception& e){error=e.what();}
    readKeyState(control,gate,snapshot,microphone);
    if(request&&error.empty())confirmKey(request->first,request->second,snapshot,microphone,error);
}
bool loadApplicationKeyPreferences(bool& osdDisabled,std::string& error) {
    KeysPreferences keys;
    if(!loadKeysPreferences(keysPreferencesPath(),keys,error))return false;
    osdDisabled=keys[KeyAction::Osd].value_or(false);
    return true;
}
bool saveOsdPreference(bool disabled,std::string& error) {
    return rememberKeyPreference(KeyAction::Osd,disabled,error);
}
std::unique_ptr<KeysOperations> createKeysOperations(bool preview,ApplicationPreferences& preferences,
        std::function<void(const ApplicationPreferences&)> applyPreferences,
        std::function<std::uint64_t(KeyAction,bool)> beginNotice,std::function<void()> ready,std::optional<std::string> presentationProfile){
    struct Owner {
        std::string profile;bool preview{};std::unique_ptr<LaptopControl> control;
        std::mutex gate;LaptopDisplayCache display;
        KeySnapshot snapshot(LaptopControlSnapshot raw,const MicrophoneState& mic){
            const auto shown=display.update(raw);KeySnapshot result;
            result.value[2]=shown.fnLock?(*shown.fnLock?1:0):-1;result.available[2]=raw.fnWritable;result.detail[2]=raw.status;
            result.value[10]=mic.enabled?(*mic.enabled?1:0):-1;result.available[10]=mic.available;result.detail[10]=mic.detail;
            result.status=mic.available?"Состояния обновляются автоматически.":mic.detail;return result;
        }
    };
    auto owner=std::make_shared<Owner>();owner->profile=deviceProfile(readBoardDescriptor()).id;owner->preview=preview;
    if(!preview)owner->control=createLaptopControl();
    KeyOperationServices services;
    services.read=[owner]{LaptopControlSnapshot snapshot;MicrophoneState mic;
        readKeyState(owner->control.get(),owner->gate,snapshot,mic);return owner->snapshot(std::move(snapshot),mic);};
    services.execute=[owner](const KeyCommand& command,KeySnapshot,const std::function<void()>&){
        LaptopControlSnapshot snapshot;MicrophoneState mic;KeyBackendResult result;
        executeKeyOperation(owner->profile,owner->preview,owner->control.get(),owner->gate,
            std::pair{command.action,command.enabled},snapshot,mic,result.error);
        audit::event("keys.observation","\"oem\":"+audit::laptop(snapshot)+",\"microphone_enabled\":"+audit::optional(mic.enabled)+
            ",\"microphone_available\":"+(mic.available?"true":"false")+",\"microphone_detail\":"+audit::quote(mic.detail));
        result.success=result.error.empty();result.snapshot=owner->snapshot(std::move(snapshot),mic);
        if(command.action==KeyAction::Microphone)result.noticeDetail=mic.detail;
        return result;
    };
    services.localState=[&preferences]{return preferences.osdDisabled;};
    services.localPreference=[&preferences,applyPreferences](bool disabled){
        std::string error;if(!saveOsdPreference(disabled,error))throw std::runtime_error("Не удалось сохранить настройку: "+error);
        auto next=preferences;next.osdDisabled=disabled;applyPreferences(next);
        KeyBackendResult result;result.success=true;return result;
    };
    services.reloadLocal=[&preferences,applyPreferences]{
        auto next=preferences;std::string error;
        if(loadApplicationKeyPreferences(next.osdDisabled,error))applyPreferences(next);
        else if(!error.empty())logMessage("keys_restore",error);
    };
    services.lifecycleRead=[owner](KeyAction action){return observeKeyForRestore(owner->profile,owner->preview,owner->control.get(),owner->gate,action);};
    services.lifecycleSet=[owner](KeyAction action,bool on){applyKey(owner->profile,owner->preview,owner->control.get(),owner->gate,action,on);};
    services.beginNotice=std::move(beginNotice);
    return std::make_unique<KeysOperations>(owner->profile,preview,true,std::move(services),std::move(ready),std::move(presentationProfile));
}

}
