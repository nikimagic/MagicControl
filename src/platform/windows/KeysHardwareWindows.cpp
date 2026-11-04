#include "KeysHardwareWindows.h"
#include "CopilotFirmwareWindows.h"
#include "MicrophoneKeysWindows.h"
#include "WindowsDevices.h"
#include "PowerDiagnosticsWindows.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/Logging.h"
#include "MagicControl/QuickSwitch.h"
#include "MagicControl/KeysBackendPolicy.h"
#include "MagicControl/KeyResourceControl.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include <array>
#include <stdexcept>

namespace mc::windows {
namespace {
constexpr std::array<DeviceKind,5> deviceKinds{DeviceKind::Wifi,DeviceKind::Bluetooth,
    DeviceKind::Camera,DeviceKind::Touchpad,DeviceKind::Microphone};
static_assert(deviceKinds.size()==kKeyCount-static_cast<std::size_t>(KeyAction::Wifi));
static_assert(deviceKinds[static_cast<std::size_t>(KeyAction::Microphone)-static_cast<std::size_t>(KeyAction::Wifi)]==DeviceKind::Microphone);
QuickSwitch quickKey(KeyAction action){return action==KeyAction::Windows?QuickSwitch::Windows:
    action==KeyAction::Fn?QuickSwitch::Fn:action==KeyAction::Usb?QuickSwitch::Usb:QuickSwitch::Ac;}
struct KeyResourceRequest {KeyAction action;bool on;KeyApplyMode mode{KeyApplyMode::Set};};
using KeyControlTable=ResourceControlTable<KeyResourceRequest,KeysHardwareContext,void>;
void setCopilotResource(const KeyResourceRequest& request,const KeysHardwareContext& c){applyCopilotFirmware(c,request.on);}
void setMicrophoneResource(const KeyResourceRequest& request,const KeysHardwareContext& c){applyMicrophoneKey(c,request.on,request.mode==KeyApplyMode::Observe);}
void setQuickKeyResource(const KeyResourceRequest& request,const KeysHardwareContext& c){
    if(request.action==KeyAction::Fn&&c.laptop){c.laptop->setFnLock(request.on);
        if(c.laptop->read().fnLock!=request.on)throw std::runtime_error("Fn: итоговое состояние не подтверждено");return;}
    if(keysBackend(c.profile,request.action,false,c.preview)!=KeysBackend::UniwillEc)throw std::runtime_error("EC недоступен.");
    const auto key=quickKey(request.action);
    if(c.native){c.native->setQuickSwitch(key,request.on);return;}
    if(!c.ec)throw std::runtime_error("EC недоступен.");
    setQuickSwitch(*c.ec,key,request.on);
}
void setSystemKeyResource(const KeyResourceRequest& request,const KeysHardwareContext& c){
    const auto index=static_cast<int>(request.action);
    // Camera owns only 727 mask 0x18, not the fan gate 0x40.
    if(request.action==KeyAction::Camera&&keysCameraEcAvailable(c)){setCamera(*c.ec,request.on);return;}
    setDevice(deviceKinds[index-6],request.on);
}
const KeyControlTable& keyControlRoutes(){
    static const auto table=[] {
        std::vector<KeyControlTable::Binding> bindings;
        constexpr std::array actions{KeyAction::Windows,KeyAction::Fn,KeyAction::Usb,KeyAction::Ac,KeyAction::Copilot,
            KeyAction::Wifi,KeyAction::Bluetooth,KeyAction::Camera,KeyAction::Touchpad,KeyAction::Microphone};
        for(const auto action:actions){const auto key=keyResourceControlKey(action);
            bindings.push_back({key.kind,key.instance,key.operation,
                action==KeyAction::Copilot?setCopilotResource:action==KeyAction::Microphone?setMicrophoneResource:static_cast<int>(action)>=6?setSystemKeyResource:setQuickKeyResource});}
        return KeyControlTable(bindings);
    }();
    return table;
}
}
bool keysCameraEcAvailable(const KeysHardwareContext& c){
    return !c.preview&&c.ec&&c.ec->boardVerified()&&c.ec->info().writable&&c.profile=="wu-jie-15-xpro";
}
DeviceState readKeySystemDevice(const KeysHardwareContext& c,KeyAction action){
    const int index=static_cast<int>(action);
    if(index<6||index>=static_cast<int>(kKeyCount))throw std::out_of_range("KEYs device action");
    if(action==KeyAction::Microphone)return readMicrophoneKey(c);
    return index==8&&keysCameraEcAvailable(c)?readCamera(*c.ec):readDevice(deviceKinds[index-6]);
}
KeyObservation observeKeyHardware(const KeysHardwareContext& c,KeyAction action){
    const auto route=keysBackend(c.profile,action,false,c.preview);
    if(route==KeysBackend::Unavailable)return {{},false,false,"No confirmed hardware adapter"};
    if(action==KeyAction::Copilot)return observeCopilotFirmware(c);
    if(action==KeyAction::Microphone)return observeMicrophoneKey(c);
    const auto index=static_cast<int>(action);
    if(index>=6){
        const auto actual=readKeySystemDevice(c,action);
        return {actual.enabled,true,actual.available,actual.detail};
    }
    if(action==KeyAction::Fn&&c.laptop){const auto actual=c.laptop->read();return {actual.fnLock,true,actual.fnWritable,actual.status};}
    const auto key=quickKey(action);
    if(c.native){const auto states=c.native->readQuickSwitches();const auto& actual=states[static_cast<std::size_t>(key)];return {actual.enabled,true,actual.writable,actual.detail};}
    if(c.ec&&c.ec->boardVerified())return {readQuickSwitch(*c.ec,key),true,c.ec->info().writable,"Uniwill quick switch"};
    return {{},true,false,"EC transport unavailable"};
}
void applyKeyHardware(const KeysHardwareContext& c,KeyAction action,bool on,KeyApplyMode mode){
    const auto resourceKey=keyResourceControlKey(action);
    const auto backend=keysBackend(c.profile,action,false,c.preview);
    if(backend==KeysBackend::Unavailable)throw std::runtime_error("Для этой модели нет подтверждённого аппаратного обработчика.");
    if(mode==KeyApplyMode::Observe&&action!=KeyAction::Microphone)throw std::invalid_argument("KEYs observation has no matching native indication handler");
    // Shared EC bytes: Fn owns 74E.4, USB owns 767.4, AC recovery owns 726.3.
    // Preserve rainbow/lightbar in 767 and fan Custom bit 7 in 726.
    if(!keyControlRoutes().hasRoute(resourceKey))throw std::runtime_error("EC недоступен.");
    keyControlRoutes().execute(resourceKey,KeyResourceRequest{action,on,mode},c);
}
KeySnapshot readKeysHardware(const KeysHardwareContext& c){
    KeySnapshot result;result.retainForDisplay.fill(true);
    const auto readEc=[&](int index,QuickSwitch key){
        try{
            if(keysBackend(c.profile,static_cast<KeyAction>(index),false,c.preview)!=KeysBackend::UniwillEc)
                throw std::runtime_error("Для этой модели нет подтверждённого аппаратного обработчика.");
            if(!c.ec||!c.ec->boardVerified())throw std::runtime_error("EC недоступен.");
            result.value[index]=readQuickSwitch(*c.ec,key);
            result.available[index]=c.ec->info().writable;
        }catch(const std::exception& error){result.detail[index]=error.what();}
    };
    if(c.native){
        const auto states=c.native->readQuickSwitches();
        constexpr std::array<int,4> indices{0,2,3,4};
        for(std::size_t i=0;i<indices.size();++i){const auto row=indices[i];
            result.value[row]=states[i].enabled?(*states[i].enabled?1:0):-1;
            result.available[row]=states[i].writable;result.detail[row]=states[i].detail;
        }
    }else{
        readEc(0,QuickSwitch::Windows);readEc(2,QuickSwitch::Fn);
        readEc(3,QuickSwitch::Usb);readEc(4,QuickSwitch::Ac);
    }
    if(c.laptop)try{
        const auto snapshot=c.laptop->read();
        result.value[2]=snapshot.fnLock?(*snapshot.fnLock?1:0):-1;
        result.available[2]=snapshot.fnWritable;
        result.detail[2]=snapshot.fnLock?"Huawei HWMI · основное/дополнительное действие F1–F12":snapshot.status;
    }catch(const std::exception& error){result.detail[2]=error.what();}
    const auto copilot=observeKeyHardware(c,KeyAction::Copilot);
    result.value[5]=copilot.enabled?(*copilot.enabled?1:0):-1;
    result.available[5]=copilot.available;result.detail[5]=copilot.detail;
    for(int i=0;i<static_cast<int>(deviceKinds.size());++i)try{
        const auto state=readKeySystemDevice(c,static_cast<KeyAction>(i+6));
        result.value[6+i]=state.enabled?(*state.enabled?1:0):-1;
        result.detail[6+i]=state.detail;result.available[6+i]=state.available;
    }catch(const std::exception& error){result.detail[6+i]=error.what();}
    return result;
}
KeySnapshot composeKeysSnapshot(KeySnapshot snapshot,bool osdDisabled){
    snapshot.available[1]=true;
    snapshot.value[1]=osdDisabled;
    snapshot.detail[1]="Отключает наши уведомления OSD. Выбор сохраняется в settings.";
    if(snapshot.value[0]>=0)snapshot.detail[0]="Вкл. — клавиша Windows заблокирована.";
    if(snapshot.value[2]>=0)snapshot.detail[2]="Меняет основное и дополнительное действие F1–F12.";
    if(snapshot.value[3]>=0)snapshot.detail[3]="Питание USB в EC; фактическое питание проверяется при выключении или гибернации.";
    if(snapshot.value[4]>=0)snapshot.detail[4]="Флаг EC. Автозапуск проверяется после перезагрузки и подключения адаптера.";
    return snapshot;
}
std::function<void()> prepareKeysRestoreRadio(std::string_view profile){
    std::function<void()> access;
    try{
        KeysPreferences saved;std::string error;
        if(loadKeysPreferences(keysPreferencesPath(profile),saved,error)&&saved[KeyAction::Wifi])
            access=requestRadioAccessForUser();
    }catch(const std::exception& e){logMessage("keys_restore",e.what());}
    return access;
}
void applyKeyPreferenceTransaction(KeyAction action,bool on,const ApplicationPreferences& previous,
        const std::function<void(const ApplicationPreferences&)>& apply,const std::function<void()>& finalize){
    if(action!=KeyAction::Osd)throw std::invalid_argument("KEYs: not an application preference");
    try{
        auto next=previous;
        next.osdDisabled=on;
        apply(next);
        std::string error;
        if(!rememberKeyPreference(action,on,error))throw std::runtime_error("KEYs: "+error);
        finalize();
    }catch(const std::exception&){
        try{apply(previous);}catch(const std::exception& rollback){logMessage("keys",std::string("preference rollback failed: ")+rollback.what());}
        throw;
    }
}
KeyBackendResult executeKeyHardwareCommand(const KeysHardwareContext& context,KeyAction action,bool on,
        const std::function<void()>& radioAccess,KeySnapshot baseline){
    KeyBackendResult result;result.snapshot=std::move(baseline);
    try{
        if(radioAccess)radioAccess();
        applyKeyHardware(context,action,on);
        const auto actual=observeKeyHardware(context,action);const auto row=static_cast<std::size_t>(action);
        result.snapshot.value[row]=actual.enabled?(*actual.enabled?1:0):-1;
        result.snapshot.available[row]=actual.available;result.snapshot.detail[row]=actual.detail;
        result.success=completeKeyResource(action,on,actual,result.error);
    }catch(const std::exception& error){result.error=error.what();}
    return result;
}
std::unique_ptr<KeysOperations> createKeysOperations(EcAccess* ec,bool preview,ApplicationPreferences& preferences,
        std::function<void(const ApplicationPreferences&)> applyPreferences,
        std::function<std::uint64_t(KeyAction,bool)> beginNotice,std::function<void()> ready){
    struct Owner {
        std::string profile;bool preview{};EcAccess* ec{};
        std::unique_ptr<LaptopControl> laptop;std::shared_ptr<Gm6Controller> native;
        KeysHardwareContext context()const{return {profile,preview,ec,laptop.get(),native.get()};}
    };
    auto owner=std::make_shared<Owner>();owner->profile=deviceProfile(readBoardDescriptor()).id;owner->ec=ec;owner->preview=preview;
    if(!ec&&!preview)owner->laptop=createLaptopControl();
    if(!preview)owner->native=gm6Controller();
    KeyOperationServices services;
    services.read=[owner]{return composeKeysSnapshot(readKeysHardware(owner->context()),false);};
    services.execute=[owner](const KeyCommand& command,KeySnapshot snapshot,const std::function<void()>& access){
        return executeKeyHardwareCommand(owner->context(),command.action,command.enabled,access,std::move(snapshot));
    };
    services.localState=[&preferences]{return preferences.osdDisabled;};
    services.localPreference=[&preferences,applyPreferences=std::move(applyPreferences)](bool disabled){
        const auto previous=preferences;
        applyKeyPreferenceTransaction(KeyAction::Osd,disabled,previous,applyPreferences,[]{});
        KeyBackendResult result;result.success=true;return result;
    };
    services.prepare=[](KeyAction action)->std::function<void()>{
        return action==KeyAction::Wifi||action==KeyAction::Bluetooth?requestRadioAccessForUser():std::function<void()>{};
    };
    services.prepareRestore=[owner]{return prepareKeysRestoreRadio(owner->profile);};
    services.lifecycleRead=[owner](KeyAction action){return observeKeyHardware(owner->context(),action);};
    services.lifecycleSet=[owner](KeyAction action,bool on){applyKeyHardware(owner->context(),action,on);};
    services.lifecycleObserve=[owner](KeyAction action){applyKeyHardware(owner->context(),action,false,KeyApplyMode::Observe);};
    services.beginNotice=std::move(beginNotice);
    services.record=[](std::string_view stage,int row,std::string_view detail){recordPowerAction("key_switch",stage,row,detail);};
    return std::make_unique<KeysOperations>(owner->profile,preview,false,std::move(services),std::move(ready));
}

}
