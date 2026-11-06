#include "MagicControl/ResourceControl.h"
#include "MagicControl/KeysLifecycle.h"
#include "MagicControl/KeyResourceControl.h"
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/PortableSettings.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/Logging.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace mc {
using namespace std::chrono_literals;
namespace {
// A dedicated KEYs file/schema. No DevicePreferences value rules are used here.
constexpr std::array<std::string_view,kKeyCount> keyNames{
    "WindowsLocked","OsdDisabled","FnLocked","UsbCharging","AcRecovery","CopilotBlocked",
    "Wifi","Bluetooth","Camera","Touchpad","Microphone"};
std::mutex keysSettingsGate;
void normalizeKeyLine(std::string& line){
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line.starts_with("\xEF\xBB\xBF"))line.erase(0,3);
}
bool saveKeysPreferences(const KeysPreferences& saved,std::string& error){
    std::ostringstream output;output<<"Version=3\r\n";
    for(std::size_t row=0;row<kKeyCount;++row)if(storedKeyPreference(static_cast<KeyAction>(row))&&saved.values[row].has_value())
        output<<keyNames[row]<<'='<<(*saved.values[row]?1:0)<<"\r\n";
    return savePortableText(keysPreferencesPath(),output.str(),error);
}
}
std::filesystem::path keysPreferencesPath(std::string_view){return portableSettingsPath("keys-settings.ini");}
bool loadKeysPreferences(const std::filesystem::path& path,KeysPreferences& saved,std::string& error){
    error.clear();
    try{
        const auto source=settingsReadPath(path);
        if(!std::filesystem::exists(source))return false;
        std::ifstream input(source,std::ios::binary);
        if(!input){error="KEYs: cannot read "+path.string();return false;}
        KeysPreferences next;std::string line;bool version=false;
        while(std::getline(input,line)){
            normalizeKeyLine(line);if(line.empty()||line.front()=='#')continue;
            const auto equal=line.find('=');
            if(equal==std::string::npos){error="KEYs: expected name=0 or name=1";return false;}
            const std::string_view name(line.data(),equal),value(line.data()+equal+1,line.size()-equal-1);
            if(name=="Version"){
                if(version||(value!="1"&&value!="2"&&value!="3")){error="KEYs: unsupported/duplicate file version";return false;}
                version=true;continue;
            }
            const auto found=std::find(keyNames.begin(),keyNames.end(),name);
            if(name.empty()||found==keyNames.end()){error="KEYs: unknown field "+std::string(name);return false;}
            const auto row=static_cast<std::size_t>(found-keyNames.begin());
            if(next.values[row].has_value()||(value!="0"&&value!="1")){error="KEYs: "+std::string(name)+" must be a unique 0/1 switch";return false;}
            next.values[row]=value=="1";
        }
        if(input.bad()){error="KEYs: file read failed";return false;}
        if(!version){error="KEYs: missing file version";return false;}
        saved=next;return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void migrateKeysPreferences(std::string_view profile){
    const std::lock_guard lock(keysSettingsGate);
    KeysPreferences patch;std::string error;
    const bool existing=loadKeysPreferences(keysPreferencesPath(),patch,error);
    if(!existing&&!error.empty())throw std::runtime_error("KEYs migration: "+error);
    bool changed=!existing;
    if(!existing){
        auto legacy=portableSettingsPath("device-settings.ini");
        if(ramFanControlProfile(profile))legacy=portableSettingsPath(std::string(gmSettings(profile)));
        else if(profile=="huawei-hlyl-wxx9")legacy=portableSettingsPath("hlyl-wxx9-device-settings.ini");
        const auto source=settingsReadPath(legacy);
        if(std::filesystem::exists(source)){
            std::ifstream input(source,std::ios::binary);std::string line;
            if(!input)throw std::runtime_error("KEYs migration: cannot read legacy file");
            while(std::getline(input,line)){
                normalizeKeyLine(line);const auto equal=line.find('=');if(equal==std::string::npos)continue;
                const std::string_view name(line.data(),equal),value(line.data()+equal+1,line.size()-equal-1);
                const auto found=std::find(keyNames.begin(),keyNames.end(),name);
                if(found==keyNames.end())continue;
                const auto row=static_cast<std::size_t>(found-keyNames.begin());
                if(row==static_cast<std::size_t>(KeyAction::Osd)||row==static_cast<std::size_t>(KeyAction::Copilot))continue;
                if(value!="0"&&value!="1")throw std::runtime_error("KEYs migration: invalid 0/1 switch "+std::string(name));
                patch.values[row]=value=="1";
            }
            if(input.bad())throw std::runtime_error("KEYs migration: legacy read failed");
        }
    }
    // Move the final two KEYs fields out of app-settings, preserving prior KEYs
    // choices. Only these exact names are consumed; other application data stays.
    const auto app=portableSettingsPath("app-settings.ini"),source=settingsReadPath(app);
    std::string original,cleaned;bool legacyApplicationKeys=false;
    if(std::filesystem::exists(source)){
        std::ifstream input(source,std::ios::binary);
        if(!input)throw std::runtime_error("KEYs migration: cannot read application legacy fields");
        original.assign(std::istreambuf_iterator<char>(input),{});
        if(input.bad())throw std::runtime_error("KEYs migration: application read failed");
        std::istringstream lines(original);std::string raw;
        while(std::getline(lines,raw)){
            auto line=raw;normalizeKeyLine(line);const auto equal=line.find('=');
            const auto name=std::string_view(line).substr(0,equal);
            std::optional<KeyAction> action;
            if(name=="OsdDisabled")action=KeyAction::Osd;
            else if(name=="CopilotBlocked")action=KeyAction::Copilot;
            if(action){
                legacyApplicationKeys=true;
                const auto value=equal==std::string::npos?std::string_view{}:std::string_view(line).substr(equal+1);
                if(value!="0"&&value!="1")throw std::runtime_error("KEYs migration: invalid legacy application switch");
                if(!patch[*action].has_value()){patch[*action]=value=="1";changed=true;}
            }else cleaned+=raw+'\n';
        }
    }
    for(auto action:{KeyAction::Osd,KeyAction::Copilot})if(!patch[action].has_value()){patch[action]=false;changed=true;}
    if(changed&&!saveKeysPreferences(patch,error))throw std::runtime_error("KEYs migration: "+error);
    if(legacyApplicationKeys){
        const auto backup=app.parent_path()/"legacy-backup"/"app-settings-before-keys-v3.ini";
        if(!std::filesystem::exists(backup)&&!savePortableText(backup,original,error))throw std::runtime_error("KEYs migration backup: "+error);
        if(!savePortableText(app,cleaned,error))throw std::runtime_error("KEYs migration cleanup: "+error);
    }
}
bool rememberKeyPreference(KeyAction action,bool enabled,std::string& error){
    if(!storedKeyPreference(action)){error="KEYs: invalid switch";return false;}
    const std::lock_guard lock(keysSettingsGate);
    KeysPreferences saved;
    if(!loadKeysPreferences(keysPreferencesPath(),saved,error)&&!error.empty())return false;
    saved[action]=enabled;
    return saveKeysPreferences(saved,error);
}
struct KeysLifecycle::Impl {
    std::string profile;
    Reader read;Setter set;Observer synchronizeObserved;
    std::array<bool,kKeyCount> observedPending{};
    std::mutex gate;std::condition_variable changed;
    bool suspended{};std::uint64_t epoch{};std::string pending;
    std::function<void()> preparation;
    std::chrono::steady_clock::time_point requested{};
    mc::ResourceActor worker;
    Impl(std::string id,Reader r,Setter s,Observer o):profile(std::move(id)),read(std::move(r)),set(std::move(s)),synchronizeObserved(std::move(o)),worker([this](std::stop_token stop){run(stop);}){}
    ~Impl(){worker.request_stop();changed.notify_all();worker.join();}
    bool interrupted(std::stop_token stop,std::uint64_t generation){
        std::lock_guard lock(gate);return stop.stop_requested()||suspended||generation!=epoch;
    }
    void log(std::string_view reason,std::size_t row,std::string_view status,const std::string& detail={}){
        logMessage("keys_restore","KEYS reason="+std::string(reason)+" row="+std::to_string(row)+" status="+std::string(status)+" "+detail);
    }
    void run(std::stop_token stop){
        while(!stop.stop_requested()){
            std::string reason;std::function<void()> prepare;std::uint64_t generation{};bool observing{};
            std::array<bool,kKeyCount> remaining{};
            {
                std::unique_lock lock(gate);
                changed.wait(lock,[&]{return stop.stop_requested()||(!suspended&&(!pending.empty()||std::any_of(observedPending.begin(),observedPending.end(),[](bool value){return value;})));});
                if(stop.stop_requested())return;
                while(!stop.stop_requested()&&!suspended&&!pending.empty()&&std::chrono::steady_clock::now()-requested<750ms)changed.wait_for(lock,100ms);
                if(stop.stop_requested())return;if(suspended)continue;
                observing=pending.empty();
                if(observing){remaining=std::exchange(observedPending,std::array<bool,kKeyCount>{});reason="observed_state";}
                else{remaining.fill(true);reason=std::exchange(pending,{});prepare=std::move(preparation);}
                generation=epoch;
            }
            bool radioReady=false;
            for(int attempt=0;attempt<30&&!interrupted(stop,generation);++attempt){
                bool retry=false;
                for(std::size_t row=0;row<kKeyCount&&!interrupted(stop,generation);++row){
                    if(!remaining[row])continue;const auto action=static_cast<KeyAction>(row);
                    if(!keyActionVisible(profile,action)){remaining[row]=false;continue;}
                    if(action==KeyAction::Osd){log(reason,row,"application_preference");remaining[row]=false;continue;}
                    if(!restoreKeyPreferenceOnLifecycle(action)){log(reason,row,"preserved","OS Bluetooth state");remaining[row]=false;continue;}
                    auto reservation=deviceOperations().tryBegin(DeviceOperationKind::Background);
                    if(!reservation){retry=true;continue;}
                    bool writeStarted=false;
                    try{
                        KeysPreferences saved;std::string error;
                        if(!loadKeysPreferences(keysPreferencesPath(),saved,error)&&!error.empty())throw std::runtime_error(error);
                        if(!observing&&!saved[action].has_value()){log(reason,row,"skipped","no saved choice");remaining[row]=false;continue;}
                        auto actual=read(action);
                        if(!actual.supported){log(reason,row,"unsupported",actual.detail);remaining[row]=false;continue;}
                        if(!actual.available||!actual.enabled){log(reason,row,"unconfirmed",actual.detail);retry=true;continue;}
                        const bool wanted=observing?*actual.enabled:*saved[action];
                        if(observing){
                            if(actual.reconcile&&synchronizeObserved){
                                if(interrupted(stop,generation))break;
                                writeStarted=true;synchronizeObserved(action);actual=read(action);
                            }
                        }else if(*actual.enabled!=wanted||actual.reconcile){
                            if(action==KeyAction::Wifi&&!radioReady&&prepare){prepare();radioReady=true;}
                            if(interrupted(stop,generation))break;
                            writeStarted=true;set(action,wanted);actual=read(action);
                        }
                        if(interrupted(stop,generation))break;
                        if(!actual.available||actual.enabled!=std::optional<bool>(wanted))throw std::runtime_error("fresh readback mismatch");
                        if(observing&&saved[action]!=actual.enabled){std::string error;if(!rememberKeyPreference(action,wanted,error))throw std::runtime_error(error);}
                        log(reason,row,"confirmed","value="+std::to_string(wanted)+" write="+std::to_string(writeStarted));remaining[row]=false;
                    }catch(const std::exception& e){
                        log(reason,row,writeStarted?"failed_after_command":"unconfirmed",e.what());
                        // Never repeat an ambiguous toggle or other sent command.
                        if(writeStarted)remaining[row]=false;else retry=true;
                    }
                }
                if(!retry)break;
                std::unique_lock lock(gate);changed.wait_for(lock,1s,[&]{return stop.stop_requested()||suspended||generation!=epoch;});
            }
            if(!interrupted(stop,generation))for(std::size_t row=0;row<kKeyCount;++row)if(remaining[row])log(reason,row,"incomplete","retry budget exhausted");
        }
    }
};
KeysLifecycle::KeysLifecycle(std::string profile,Reader read,Setter set,Observer synchronizeObserved){
    try{migrateKeysPreferences(profile);}catch(const std::exception& e){logMessage("keys_restore",e.what());}
    impl_=std::make_unique<Impl>(std::move(profile),std::move(read),std::move(set),std::move(synchronizeObserved));
}
KeysLifecycle::~KeysLifecycle()=default;
void KeysLifecycle::request(std::string reason,std::function<void()> preparation){
    std::lock_guard lock(impl_->gate);impl_->suspended=false;impl_->pending=std::move(reason);
    if(preparation)impl_->preparation=std::move(preparation);
    ++impl_->epoch;impl_->requested=std::chrono::steady_clock::now();impl_->changed.notify_all();
}
void KeysLifecycle::suspend(){
    std::lock_guard lock(impl_->gate);impl_->suspended=true;impl_->pending.clear();impl_->observedPending.fill(false);impl_->preparation={};++impl_->epoch;impl_->changed.notify_all();
}
void KeysLifecycle::observe(KeyAction action){
    const auto index=static_cast<std::size_t>(action);if(index>=kKeyCount)return;
    std::lock_guard lock(impl_->gate);if(impl_->suspended)return;
    impl_->observedPending[index]=true;impl_->changed.notify_all();
}

struct KeysOperations::Impl {
    struct Request {KeyCommand command;std::shared_ptr<DeviceOperation> reservation;};
    std::string profile;bool preview{},linuxHost{};
    KeyOperationServices services;std::function<void()> ready;
    std::unique_ptr<KeysLifecycle> lifecycle;
    KeySnapshot snapshot;
    std::array<bool,kKeyCount> visible{};
    std::array<std::optional<int>,kKeyCount> observed{};
    bool busy{},refreshing{},loaded{},lifecyclePaused{};
    std::optional<Request> queued;
    std::optional<KeyAction> applying;
    std::shared_ptr<DeviceOperation> operation;
    ResourceWorker worker;std::atomic<bool> done{};KeyOperationResult pending;

    Impl(std::string id,bool viewing,bool linuxPlatform,KeyOperationServices adapters,std::function<void()> notify,std::string presentationProfile)
        :profile(std::move(id)),preview(viewing),linuxHost(linuxPlatform),services(std::move(adapters)),ready(std::move(notify)){
        const auto& presentation=preview&&!presentationProfile.empty()?presentationProfile:profile;
        for(std::size_t i=0;i<kKeyCount;++i)visible[i]=keyActionVisible(presentation,static_cast<KeyAction>(i));
        localState();
        if(services.lifecycleRead&&services.lifecycleSet)
            lifecycle=std::make_unique<KeysLifecycle>(profile,services.lifecycleRead,services.lifecycleSet,services.lifecycleObserve);
    }
    ~Impl(){lifecycle.reset();worker.join();}
    void localState(){snapshot.value[1]=services.localState?services.localState():false;snapshot.available[1]=true;}
    void notify()noexcept{try{if(ready)ready();}catch(const std::exception& e){logMessage("keys",e.what());}catch(...){}}
    void record(std::string_view stage,int row,std::string_view detail){if(services.record)services.record(stage,row,detail);}
    KeyOperationResult result(KeyCommand command,KeyBackendResult backend){
        KeyOperationResult value;value.command=command;value.snapshot=std::move(backend.snapshot);value.success=backend.success;
        value.message=backend.success?(command.action==KeyAction::Osd?"Настройка применена и сохранена в keys-settings.ini.":"Изменение подтверждено."):
            !backend.error.empty()?"Ошибка: "+backend.error:"Команда выполнена, но итоговое состояние устройства не подтверждено.";
        value.noticeDetail=backend.noticeDetail.empty()?value.message:std::move(backend.noticeDetail);
        if(value.success){
            if(command.action==KeyAction::Ac)value.noticeDetail="Флаг EC подтверждён. Автозапуск проверяется после перезагрузки.";
            else if(command.action==KeyAction::Usb)value.noticeDetail="Подтверждено EC. При выключении и гибернации.";
            else if(command.action==KeyAction::Copilot)value.noticeDetail="Блокировка подтверждена прошивкой; выбор сохранён в KEYs.";
        }
        value.announceOsdEnabled=value.success&&command.action==KeyAction::Osd&&!command.enabled;
        return value;
    }
    void merge(KeySnapshot& fresh){
        for(std::size_t i=0;i<kKeyCount;++i){
            if(observed[i]){fresh.value[i]=*observed[i];observed[i].reset();}
            if(fresh.retainForDisplay[i])fresh.value[i]=retainedControlDisplayValue(fresh.value[i],snapshot.value[i]);
            if(fresh.detail[i].empty())fresh.detail[i]=snapshot.detail[i];
        }
        // The application-owned preference is authoritative even if an older
        // external notification was delivered while the worker was running.
        fresh.value[1]=services.localState?services.localState():false;fresh.available[1]=true;
        snapshot=fresh;
    }
    void publish(KeyOperationResult completed){
        pending=std::move(completed);done.store(true,std::memory_order_release);notify();
    }
    KeyOperationResult failure(std::optional<KeyCommand> command,KeySnapshot baseline,std::string error){
        if(command){KeyBackendResult failed;failed.snapshot=std::move(baseline);failed.error=std::move(error);return result(*command,std::move(failed));}
        KeyOperationResult failed;failed.snapshot=std::move(baseline);failed.message="Ошибка: "+std::move(error);return failed;
    }
    template<class Work> void startWorker(std::optional<KeyCommand> command,audit::Id id,Work work){
        worker.join();
        done.store(false,std::memory_order_release);const auto baseline=snapshot;
        try{worker.launch([this,command,id,baseline,work=std::move(work)]()mutable{
            const auto fields=command?"\"row\":"+std::to_string(static_cast<int>(command->action))+",\"requested\":"+(command->enabled?"true":"false"):std::string{};
            audit::Scope trace("keys.apply",fields,id,command.has_value());
            KeyOperationResult completed;
            try{completed=work();}
            catch(const std::exception& e){completed=failure(command,baseline,e.what());}
            catch(...){completed=failure(command,baseline,"Неизвестная ошибка KEYs");}
            if(command)trace.finish(completed.success?"readback_confirmed":"failed",
                "\"observed\":"+std::to_string(completed.snapshot.value[static_cast<std::size_t>(command->action)])+",\"message\":"+audit::quote(completed.message));
            publish(std::move(completed));
        },[]{},command?ResourceWorkClass::Interactive:ResourceWorkClass::Observation);}catch(const std::exception& e){publish(failure(command,baseline,e.what()));}
    }
    void publishFailure(const Request& request,std::string error){publish(failure(request.command,snapshot,std::move(error)));}
    void launch(Request request){
        operation=request.reservation;applying=request.command.action;busy=true;refreshing=false;
        std::function<void()> preparation;
        try{if(services.prepare)preparation=services.prepare(request.command.action);}
        catch(const std::exception& e){publishFailure(request,e.what());return;}
        const auto command=request.command;
        startWorker(command,request.reservation->actionId(),[this,command,baseline=snapshot,preparation=std::move(preparation)]()mutable{
            return result(command,services.execute(command,std::move(baseline),preparation));
        });
    }
    KeySubmission submit(int row,bool on){
        audit::Scope dispatch("keys.dispatch","\"row\":"+std::to_string(row)+",\"enabled\":"+(on?"true":"false"),{},true);
        const bool available=row>=0&&row<static_cast<int>(kKeyCount)&&snapshot.available[static_cast<std::size_t>(row)];
        const bool present=row>=0&&row<static_cast<int>(kKeyCount)&&visible[static_cast<std::size_t>(row)];
        const auto plan=planKeyCommand(profile,row,preview,available,busy,refreshing,queued.has_value(),linuxHost,present);
        if(plan==KeyCommandPlan::UnsupportedRoute||plan==KeyCommandPlan::Unavailable||plan==KeyCommandPlan::Busy){
            dispatch.finish("rejected","\"reason\":"+audit::quote(plan==KeyCommandPlan::Busy?"busy":"unsupported_or_unavailable"));return {};
        }
        auto reservation=deviceOperations().tryBegin();
        if(!reservation){dispatch.finish("rejected","\"reason\":\"operation_gate_busy\"");return {false,"Ожидайте завершения текущего переключения EC.",{}};}
        Request request{{static_cast<KeyAction>(row),on,0},std::move(reservation)};
        record("requested",row,on?"on":"off");
        try{if(services.beginNotice)request.command.noticeToken=services.beginNotice(request.command.action,on);}
        catch(const std::exception& e){dispatch.fail(e.what());return {false,e.what(),{}};}
        if(plan==KeyCommandPlan::LocalPreference){
            KeyBackendResult backend;backend.snapshot=snapshot;
            try{if(!services.localPreference)throw std::runtime_error("KEYs: OSD adapter unavailable");
                auto applied=services.localPreference(on);backend.success=applied.success;backend.error=std::move(applied.error);}
            catch(const std::exception& e){backend.error=e.what();}
            localState();backend.snapshot=snapshot;
            auto completed=result(request.command,std::move(backend));
            record(completed.success?"confirmed":"failed",row,completed.message);
            dispatch.finish(completed.success?"configuration_saved":"failed","\"message\":"+audit::quote(completed.message));
            return {true,completed.message,std::move(completed)};
        }
        if(plan==KeyCommandPlan::Queue){queued=std::move(request);dispatch.finish("queued_after_read");return {true,"Применю после обновления состояния…",{}};}
        launch(std::move(request));dispatch.finish("queued");return {true,"Применяю изменение…",{}};
    }
    bool refresh(){
        if(busy||deviceOperations().busy())return false;
        busy=true;refreshing=true;applying.reset();
        startWorker({},0,[this]{KeyOperationResult reading;reading.snapshot=services.read();reading.success=true;return reading;});
        return true;
    }
    std::optional<KeyOperationResult> take(){
        if(!done.exchange(false,std::memory_order_acq_rel))return {};
        worker.join();
        auto completed=std::move(pending);merge(completed.snapshot);
        if(!completed.command){
            if(completed.message.empty()){
                if(!completed.snapshot.status.empty())completed.message=completed.snapshot.status;
                else if(!loaded)completed.message="Состояния обновляются автоматически.";
            }
        }else record(completed.success?"confirmed":"failed",static_cast<int>(completed.command->action),completed.message);
        operation.reset();applying.reset();busy=false;refreshing=false;loaded=true;
        if(queued){
            auto request=std::move(*queued);queued.reset();const auto row=static_cast<std::size_t>(request.command.action);
            if(!snapshot.available[row]){operation=request.reservation;busy=true;publishFailure(request,"Устройство недоступно после обновления состояния.");}
            else launch(std::move(request));
        }
        return completed;
    }
};
KeysOperations::KeysOperations(std::string profile,bool preview,bool linuxHost,KeyOperationServices services,std::function<void()> ready,std::string presentationProfile)
    :impl_(std::make_unique<Impl>(std::move(profile),preview,linuxHost,std::move(services),std::move(ready),std::move(presentationProfile))){}
KeysOperations::~KeysOperations()=default;
KeySubmission KeysOperations::submit(int row,bool on){return impl_->submit(row,on);}
bool KeysOperations::refresh(){return impl_->refresh();}
std::optional<KeyOperationResult> KeysOperations::take(){return impl_->take();}
const KeySnapshot& KeysOperations::snapshot()const noexcept{return impl_->snapshot;}
bool KeysOperations::visible(int row)const noexcept{return row>=0&&row<static_cast<int>(kKeyCount)&&impl_->visible[static_cast<std::size_t>(row)];}
const std::array<bool,kKeyCount>& KeysOperations::visibleRows()const noexcept{return impl_->visible;}
bool KeysOperations::disabled(int row)const noexcept{
    return row<0||row>=static_cast<int>(kKeyCount)||deviceOperations().interactiveBusy()||
        keysBackend(impl_->profile,static_cast<KeyAction>(row),impl_->linuxHost,impl_->preview,impl_->visible[static_cast<std::size_t>(row)])==KeysBackend::Unavailable||
        (impl_->busy&&(!impl_->refreshing||impl_->queued)&&row!=1)||!impl_->snapshot.available[static_cast<std::size_t>(row)];
}
void KeysOperations::observe(int row,bool on){
    if(row<0||row>=static_cast<int>(kKeyCount)||(impl_->preview&&row<6))return;
    if(row==static_cast<int>(KeyAction::Microphone)&&impl_->lifecycle)impl_->lifecycle->observe(KeyAction::Microphone);
    // A command confirms its own row from fresh hardware. Notifications for
    // other rows must survive that command's older baseline snapshot as well.
    if(impl_->refreshing||(impl_->busy&&impl_->applying!=static_cast<KeyAction>(row)))
        impl_->observed[static_cast<std::size_t>(row)]=on?1:0;
    impl_->snapshot.value[static_cast<std::size_t>(row)]=on?1:0;
}
void KeysOperations::restore(std::string reason){
    impl_->lifecyclePaused=false;
    if(impl_->services.reloadLocal)impl_->services.reloadLocal();
    impl_->localState();
    if(impl_->lifecycle)impl_->lifecycle->request(std::move(reason),impl_->services.prepareRestore?impl_->services.prepareRestore():std::function<void()>{});
}
void KeysOperations::suspend(){impl_->lifecyclePaused=true;if(impl_->lifecycle)impl_->lifecycle->suspend();}
void KeysOperations::powerSleep(bool sleeping){
    if(sleeping){if(impl_->lifecycle)impl_->lifecycle->suspend();}
    else if(!impl_->lifecyclePaused)restore("power_resume");
}

}
