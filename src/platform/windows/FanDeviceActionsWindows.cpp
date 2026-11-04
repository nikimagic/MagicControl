#include "FanDeviceActionsWindows.h"
#include "FanCurveStorageWindows.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/FanResourceControl.h"
#include <exception>
#include <utility>
#include <chrono>
#include <thread>
#include "TextWindows.h"
#include "MagicControl/FanControlLimits.h"

namespace mc::windows {
namespace {
struct GmFanWrite {const gm6::Curves& curves;std::array<bool,2> controlled;bool linked;};
struct GmFanContext {Gm6Controller& controller;};
using GmFanRoutes=ResourceControlTable<GmFanWrite,GmFanContext,void>;
void gmDefault(const GmFanWrite&,const GmFanContext& c){c.controller.setAutomatic();}
void gmMax(const GmFanWrite&,const GmFanContext& c){c.controller.setBoost(true);}
void gmOnline(const GmFanWrite& r,const GmFanContext& c){c.controller.setOnlineCurves(r.curves,r.controlled,r.linked);}
void gmManual(const GmFanWrite& r,const GmFanContext& c){c.controller.setCurves(r.curves);}
const GmFanRoutes& gmFanRoutes(){
    static const auto routes=[] {
        std::vector<GmFanRoutes::Binding> bindings;
        const auto add=[&](FanResourceOperation operation,GmFanRoutes::Handler handler){const auto key=fanResourceControlKey(operation);bindings.push_back({key.kind,key.instance,key.operation,handler});};
        add(FanResourceOperation::Default,gmDefault);add(FanResourceOperation::Maximum,gmMax);
        add(FanResourceOperation::Online,gmOnline);add(FanResourceOperation::Manual,gmManual);
        return GmFanRoutes(bindings);
    }();
    return routes;
}
struct NativeFanWrite {const std::array<int,2>& target;const std::optional<NativeFanCurves>& curves;};
struct NativeFanContext {NativeFixedFans& controller;};
using NativeFanRoutes=ResourceControlTable<NativeFanWrite,NativeFanContext,void>;
void nativeDefault(const NativeFanWrite&,const NativeFanContext& c){if(c.controller.setAutomatic()==NativeFanAutoResult::OwnershipLost)throw std::runtime_error("Native FAN ownership lost; Default not confirmed");}
void nativeManual(const NativeFanWrite& r,const NativeFanContext& c){
    if(r.curves)c.controller.setCurves(*r.curves);else c.controller.setFixed(r.target);
}
void nativeMaximum(const NativeFanWrite&,const NativeFanContext& c){c.controller.setMaximum();}
const NativeFanRoutes& nativeFanRoutes(){
    static const auto routes=[] {
        std::vector<NativeFanRoutes::Binding> bindings;
        const auto add=[&](FanResourceOperation operation,NativeFanRoutes::Handler handler){const auto key=fanResourceControlKey(operation);bindings.push_back({key.kind,key.instance,key.operation,handler});};
        add(FanResourceOperation::Default,nativeDefault);add(FanResourceOperation::Manual,nativeManual);
        add(FanResourceOperation::Maximum,nativeMaximum);add(FanResourceOperation::Minimum,nativeManual);
        return NativeFanRoutes(bindings);
    }();
    return routes;
}
FanResourceOperation gmFanOperation(int mode){return mode==0?FanResourceOperation::Default:mode==2?FanResourceOperation::Maximum:mode==3?FanResourceOperation::Online:FanResourceOperation::Manual;}
FanResourceOperation nativeFanOperation(int mode){return mode==0?FanResourceOperation::Default:mode==2?FanResourceOperation::Maximum:mode==3?FanResourceOperation::Minimum:FanResourceOperation::Manual;}
struct EcFanWrite {const FanCurveTable* curve{};const FanCurveProfiles* profiles{};std::shared_ptr<DeviceOperation> operation;bool wait{},resetConsumers{},adoptExisting{};};
struct EcFanContext {FanController& controller;};
using EcFanRoutes=ResourceControlTable<EcFanWrite,EcFanContext,void>;
void ecManual(const EcFanWrite& r,const EcFanContext& c){
    if(!r.curve)throw std::invalid_argument("FANs: manual curve missing");
    if(r.wait)c.controller.ecManualAndWait(*r.curve,r.operation).get();else c.controller.ecManual(*r.curve,*r.curve,r.operation);
}
void ecDefault(const EcFanWrite& r,const EcFanContext& c){
    if(r.wait)c.controller.nativeAndWait(r.operation,r.resetConsumers).get();else c.controller.native(r.operation,r.resetConsumers);
}
void ecMax(const EcFanWrite& r,const EcFanContext& c){c.controller.maximum(r.operation);}
void ecRestore(const EcFanWrite& r,const EcFanContext& c){
    if(!r.profiles)throw std::invalid_argument("FANs: profiles missing");
    c.controller.restoreSavedProfiles(*r.profiles,r.operation,r.adoptExisting);
}
void ecOnline(const EcFanWrite& r,const EcFanContext& c){
    if(!r.profiles)throw std::invalid_argument("FANs: profiles missing");
    auto profiles=*r.profiles;profiles.ecManual=false; // Intent only, not curve conversion.
    c.controller.restoreSavedProfiles(profiles,r.operation);
}
const EcFanRoutes& ecFanRoutes(){
    static const auto routes=[] {
        std::vector<EcFanRoutes::Binding> bindings;
        const auto add=[&](FanResourceOperation operation,EcFanRoutes::Handler handler){const auto key=fanResourceControlKey(operation);bindings.push_back({key.kind,key.instance,key.operation,handler});};
        add(FanResourceOperation::Manual,ecManual);add(FanResourceOperation::Default,ecDefault);
        add(FanResourceOperation::Maximum,ecMax);add(FanResourceOperation::Restore,ecRestore);
        add(FanResourceOperation::Online,ecOnline);
        return EcFanRoutes(bindings);
    }();
    return routes;
}
void dispatchGm(Gm6Controller& controller,FanResourceOperation operation,const gm6::Curves& curves,std::array<bool,2> controlled,bool linked){
    gmFanRoutes().execute(fanResourceControlKey(operation),GmFanWrite{curves,controlled,linked},GmFanContext{controller});
}
void dispatchNative(NativeFixedFans& controller,FanResourceOperation operation,const std::array<int,2>& target,const std::optional<NativeFanCurves>& curves){
    nativeFanRoutes().execute(fanResourceControlKey(operation),NativeFanWrite{target,curves},NativeFanContext{controller});
}
void dispatchEc(FanController& controller,const FanWriteRequest& request,std::shared_ptr<DeviceOperation> operation){
    ecFanRoutes().execute(fanResourceControlKey(request.operation),
        EcFanWrite{request.ecCurve?&*request.ecCurve:nullptr,request.profiles?&*request.profiles:nullptr,std::move(operation),request.wait,request.origin==FanWriteOrigin::Explicit,request.adoptExisting},EcFanContext{controller});
}
} // model command adapters
const char* gmFanAdmissionReason(bool preview,bool busy,bool hasController,bool connected,bool writable) noexcept{
    if(preview)return "model_preview";
    if(busy)return "busy";
    if(!hasController)return "no_supported_backend";
    if(!connected)return "not_connected";
    if(!writable)return "not_writable";
    return nullptr;
}
const char* nativeFanAdmissionReason(bool busy,bool hasController,bool known,bool writable) noexcept{
    if(busy)return "busy";
    if(!hasController)return "no_supported_backend";
    if(!known)return "state_unknown";
    if(!writable)return "not_writable";
    return nullptr;
}
std::optional<int> nativeFanPresetMode(FanCurvePreset preset,bool nativeCurves) noexcept{
    if(preset==FanCurvePreset::Default)return 0;
    if(preset==FanCurvePreset::Max)return 2;
    if(nativeCurves&&preset==FanCurvePreset::Manual)return 1;
    if(nativeCurves&&preset==FanCurvePreset::Min)return 3;
    return {};
}
void selectFanPresetIntent(FanCurveProfiles& profiles,int channel,FanCurvePreset preset){
    profiles.ecManual=false;
    if(preset==FanCurvePreset::Default||preset==FanCurvePreset::Max)profiles.selected.fill(preset);
    else{
        profiles.select(channel,preset);
        if(!profiles.linked&&profiles.currentPreset(1-channel)==FanCurvePreset::Max)
            profiles.select(1-channel,FanCurvePreset::Default);
    }
}
bool toggleGmFanLinkIntent(FanCurveProfiles& profiles){
    const bool before=profiles.linked;
    profiles.linked=!before;
    return before;
}
void toggleOnlineFanLinkIntent(FanCurveProfiles& profiles,const FanControlStatus& mode){
    const bool wasLinked=profiles.linked;
    // Preserve the original single exact-table copy and selections; do not
    // recalculate, resample, or alter the number of fan curve points.
    if(!wasLinked&&profiles.currentPreset(0)==FanCurvePreset::Default&&mode.controlled[1])
        profiles.curves[static_cast<std::size_t>(FanCurvePreset::Manual)][0]=mode.applied[1];
    profiles.selected.fill(FanCurvePreset::Manual);
    profiles.ecManual=false;
    profiles.linked=!wasLinked;
}
std::array<bool,2> fanOnlineControlled(bool online,std::array<bool,2> current,int channel,bool linked,int mode){
    auto controlled=online?current:std::array<bool,2>{};
    if(channel>=0&&channel<2)controlled[channel]=true;
    if(mode==3&&linked)controlled={true,true};
    return controlled;
}
std::array<int,2> nativeFanTarget(int mode,const std::array<int,2>& minimum,const std::array<int,2>& draft){
    return mode==2?std::array<int,2>{100,100}:mode==3?minimum:draft;
}
namespace {
struct GmFanOperationResult {
    bool success{},readbackMismatch{},readFailed{};std::string error{},readError{};Gm6State actual{};
};
struct NativeFanOperationResult {
    bool success{},readbackMismatch{},commandFailed{},readFailed{};std::string commandError{},readError{};
    std::optional<NativeFixedFanState> actual{};
};
GmFanOperationResult applyGmFanOperation(Gm6Controller& controller,int mode,
        const gm6::Curves& curves,std::array<bool,2> controlled,bool linked){
    GmFanOperationResult result;
    try {
        dispatchGm(controller,gmFanOperation(mode),curves,controlled,linked);
        result.success=true;
    }catch(const std::exception& e){result.error=e.what();}
    // Retain the prior unconditional read, even when the command fails.
    try{result.actual=controller.read(true);}catch(const std::exception& e){result.success=false;result.readFailed=true;result.readError=e.what();}
    if(result.success){
        const auto& actual=result.actual;
        bool matches=actual.connected&&actual.writable&&!actual.needsRestore;
        if(mode==0)matches=matches&&!actual.online&&!actual.boost&&!actual.custom&&!actual.ramActive&&!actual.overrideActive;
        else if(mode==2)matches=matches&&!actual.online&&actual.boost;
        else if(mode==3)matches=matches&&actual.online&&actual.custom&&actual.ramActive&&actual.overrideActive&&
            actual.onlineControlled==controlled&&actual.onlineLinked==linked&&actual.onlineCurves&&*actual.onlineCurves==curves;
        else{
            // Compare with the existing hardware encoding markers. This is a
            // temporary copy for readback only; requested curves stay untouched.
            auto expected=curves;for(auto& channel:expected){channel[0].up=0;channel[15].down=255;}
            matches=matches&&!actual.online&&!actual.boost&&actual.custom&&actual.ramActive&&actual.overrideActive&&actual.curves&&*actual.curves==expected;
        }
        if(!matches){result.success=false;result.readbackMismatch=true;}
    }
    return result;
}
std::wstring gmFanResultMessage(int mode,const GmFanOperationResult& result,
        const std::function<std::wstring(const std::string&)>& toWide){
    if(result.readFailed)return toWide(result.error.empty()?result.readError:result.error+" · "+result.readError);
    if(result.readbackMismatch)return L"GM6IR0C: повторное чтение не подтвердило выбранный режим. "+toWide(result.actual.status);
    if(result.success)return mode==3?L"Manual online включён; температуры CPU/GPU, защита85°C.":L"Режим подтверждён чтением EC.";
    return toWide(result.error);
}
NativeFanOperationResult applyNativeFanOperation(NativeFixedFans& controller,int mode,
        const std::array<int,2>& target,const std::optional<NativeFanCurves>& curves){
    NativeFanOperationResult result;
    try{
        dispatchNative(controller,nativeFanOperation(mode),target,curves);
        result.success=true;
    }catch(const std::exception& e){result.commandFailed=true;result.commandError=e.what();}
    try{
        result.actual=controller.read();
        const auto& actual=*result.actual;
        const bool targetConfirmed=curves?(actual.curves&&actual.curves->appliedCurve&&actual.curves->draft==*curves):
            actual.targetRamped&&actual.mode>=0?actual.mode==mode:actual.targetPercent==target;
        const bool confirmed=mode==0?!actual.manual:(actual.manual&&targetConfirmed);
        if(result.success&&!confirmed){result.success=false;result.readbackMismatch=true;}
    }catch(const std::exception& e){result.success=false;result.readFailed=true;result.readError=e.what();}
    return result;
}
std::wstring nativeFanResultMessage(bool nativeCurves,int mode,const NativeFanOperationResult& result,
        const std::function<std::wstring(const std::string&)>& toWide){
    std::wstring message;
    if(result.commandFailed)message=toWide(result.commandError);
    else if(result.actual&&result.actual->sharedCurve)message=toWide(result.actual->status);
    else message=nativeCurves?(mode==0?L"HPBSD: штатные таблицы восстановлены.":L"HPBSD: цели оборотов подтверждены; верхние температурные ступени сохранены."):
        (mode==0?L"Honor: штатная автоматика подтверждена.":L"Honor: ручной PWM подтверждён; термоконтроль активен.");
    if(result.readbackMismatch)message=result.actual&&result.actual->sharedCurve?L"EC: повторное чтение не подтвердило режим.":nativeCurves?L"HPBSD: повторное чтение не подтвердило режим.":L"Honor: повторное чтение не подтвердило выбранный режим.";
    if(result.readFailed)message+=L" · "+toWide(result.readError);
    return message;
}
} // private operation/readback policy
FanResourceResult executeFanResource(const FanResourceContext& c,const FanWriteRequest& request){
    using O=FanResourceOperation;using namespace std::chrono_literals;
    FanResourceResult out;
    if(request.origin==FanWriteOrigin::Explicit&&request.operation!=O::SaveDraft)invalidateDiagnosticFanSessions();
    audit::Scope trace("fans.execute","\"operation\":"+audit::quote(fanResourceControlKey(request.operation).operation)+",\"origin\":"+
        audit::quote(request.origin==FanWriteOrigin::Explicit?"user":request.origin==FanWriteOrigin::Restore?"restore":"compound"),c.operation?c.operation->actionId():0);
    try{
        if(c.gm){
            if(request.operation!=O::Default&&request.operation!=O::Maximum&&request.operation!=O::Manual&&request.operation!=O::Online)
                throw std::invalid_argument("GM FAN operation unavailable");
            if((request.operation==O::Manual||request.operation==O::Online)&&!request.curves)throw std::invalid_argument("GM FAN curves missing");
            const int mode=request.operation==O::Default?0:request.operation==O::Maximum?2:request.operation==O::Online?3:1;
            const auto result=applyGmFanOperation(*c.gm,mode,request.curves.value_or(gm6::Curves{}),request.controlled,request.linked);
            out.success=result.success;if(!result.readFailed)out.gm6=result.actual;
            out.message=gmFanResultMessage(mode,result,encoding::wide);
        }else if(c.native){
            std::optional<NativeFanCurves> tables;
            if(request.profiles)tables=request.profiles->curves[0];
            else if(request.curves){tables.emplace();for(int ch=0;ch<2;++ch)for(int i=0;i<16;++i)(*tables)[ch][i]={(*request.curves)[ch][i].up,(*request.curves)[ch][i].duty};}
            if(request.operation==O::SaveDraft){
                if(!tables)throw std::invalid_argument("FAN draft missing");
                c.native->saveCurveDraft(*tables);if(!c.profileId.empty())rememberFanChoice(c.profileId,request);out.success=true;out.message=L"Кривая сохранена. Нажмите Manual для применения.";
            }else{
                if(request.operation!=O::Default&&request.operation!=O::Maximum&&request.operation!=O::Minimum&&request.operation!=O::Manual)
                    throw std::invalid_argument("Native FAN operation unavailable");
                const int mode=request.operation==O::Default?0:request.operation==O::Maximum?2:request.operation==O::Minimum?3:1;
                auto target=request.target.value_or(std::array<int,2>{});
                // A restore/Custom request can carry its saved draft. Max must
                // never dispatch or confirm that payload as a Manual curve.
                if(mode==2){target={100,100};tables.reset();}
                if(mode==3){const auto before=c.native->read();if(!before.minimumSupported)throw std::runtime_error("Native FAN minimum unavailable");target=before.minimumPercent;}
                if(mode==1&&!tables&&!request.target)throw std::invalid_argument("Native FAN targets missing");
                if(mode==1&&!tables&&request.origin==FanWriteOrigin::Explicit)
                    for(const int value:target)if(!fanInput::validPercent(value))throw std::invalid_argument("FAN input must be5..100 percent");
                const auto result=applyNativeFanOperation(*c.native,mode,target,tables);
                out.success=result.success;out.native=result.actual;out.message=nativeFanResultMessage(c.nativeCurves,mode,result,encoding::wide);
            }
        }else if(c.ec){
            if(request.operation==O::Restore&&request.origin==FanWriteOrigin::Restore&&c.ec->status().needsRestore)
                c.ec->nativeAndWait(c.operation,false).get();
            const auto previous=c.ec->status().revision;
            dispatchEc(*c.ec,request,c.operation);
            if(request.wait){
                const auto deadline=std::chrono::steady_clock::now()+15s;
                for(;;){const auto actual=c.ec->status();
                    if(actual.error||actual.needsRestore)throw std::runtime_error(actual.message);
                    if(actual.revision>previous&&!actual.busy&&!actual.applying){out.ec=actual;out.success=true;out.message=encoding::wide(actual.message);break;}
                    if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("FAN command still pending; readback not confirmed");
                    std::this_thread::sleep_for(50ms);
                }
            }else{out.pending=true;out.message=L"Команда FANs поставлена в очередь.";}
        }else throw std::runtime_error("FAN control backend unavailable");
        if(c.native&&out.success&&out.native&&out.native->curves&&!c.profileId.empty()){
            rememberFanChoice(c.profileId,request,&*out.native);
        }
        if(!out.success&&!out.pending)out.error=encoding::utf8(out.message);
    }catch(const std::exception& error){out.success=out.pending=false;out.error=error.what();out.message=encoding::wide(out.error);}
    trace.finish(out.pending?"queued":out.success?(request.operation==O::SaveDraft?"draft_saved":"readback_confirmed"):"failed","\"message\":"+audit::quote(encoding::utf8(out.message)));
    return out;
}
NativeFanTableSnapshot readNativeFanTables(EcAccess& access,bool ecManual){
    const auto state=readNativeFanState(access,!ecManual);
    const auto pack=[](const std::vector<FanCurvePoint>& curve){
        FanCurveTable result{};
        for(auto& cell:result)cell={255,100};
        for(std::size_t i=0;i<curve.size()&&i<result.size();++i)result[i]={curve[i].temperature,curve[i].duty};
        return result;
    };
    return {pack(state.cpu),pack(state.gpu),state.shared,state.active,state.defaultMode,state.boost};
}
}

#include "FanCurveStorageWindows.h"
#include "PowerDiagnosticsWindows.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include "MagicControl/CustomPresets.h"
#include "../../protocols/minisforum/HpbsdFans.h"
#include "MagicControl/FanTelemetry.h"
#include <fstream>
#include <format>
namespace mc::windows {
using mc::windows::encoding::wide;
FansOperationsWindows::FansOperationsWindows(ViewPort& view,EcAccess* access,std::shared_ptr<FanController> controller,std::function<MonitoringSnapshot()> source,std::function<void(std::string)> logger,BoardDescriptor board,bool viewing)
    :ec(viewing?nullptr:access),monitoringReader(viewing?nullptr:std::make_unique<MonitoringReader>(access,false,true)),monitoringSource(std::move(source)),resourceBoard(std::move(board)),telemetryLog(std::move(logger)),preview(viewing),gm6Ui(ramFanControlProfile(deviceProfile(resourceBoard).id)),fanController(viewing?nullptr:std::move(controller)),view_(view){
        nativeCurves=!ec&&(deviceProfile(resourceBoard).id=="minisforum-hpbsd"||deviceProfile(resourceBoard).id=="lenovo-legion-kwcn");
        // Reuse the native fixed-target editor/worker for both typed protocols.
        huawei=!ec&&(deviceProfile(resourceBoard).id=="huawei-hlyl-wxx9"||nativeCurves);
        if(!preview)gm6Controller=mc::windows::gm6Controller();
        if(huawei&&!preview)hlylController=mc::windows::nativeFixedFanController();
        if(deviceProfile(resourceBoard).id=="minisforum-hpbsd"&&!preview){std::ifstream saved(mc::portableSettingsPath("hpbsd-fan-targets.txt"));int a{},b{};std::string extra;if(saved>>a>>b&&!(saved>>extra)&&mc::fanInput::validPercent(a)&&mc::fanInput::validPercent(b))hlylDraft={a,b};}
        resetCurves();
        if(preview){for(int ch=0;ch<2;++ch)profiles.setActivePointCount(ch,deviceProfile(resourceBoard).fanPointCounts[ch]);profiles.selected.fill(FanCurvePreset::Default);profiles.linked=deviceProfile(resourceBoard).id=="lenovo-legion-kwcn";}
        else loadLocalCurveSettings();
        gm6DraftKnown=gm6Ui&&localCurveSettingsLoaded;
        // A copied MECHREVO INI/CSV must not claim Huawei manual control.
        // This changes the local presentation only; the saved file is untouched.
        if(huawei){profiles.ecManual=false;profiles.selected={FanCurvePreset::Default,FanCurvePreset::Default};}

}
void FansOperationsWindows::resetCurves(){
        profiles = mc::FanCurveProfiles{};
    }
void FansOperationsWindows::loadLocalCurveSettings(){
        windows::FanCurveStorage storage{localCurveSettingsPath,profileError,localCurveSettingsLoaded};
        const auto result=windows::loadLocalFanCurves(deviceProfile(resourceBoard).id,preview,profiles,storage);
        if(result==windows::FanCurveLoadResult::PathError){localCurveStatus=wide(storage.error);return;}
        localCurveSettingsPath=std::move(storage.path);profileError=std::move(storage.error);localCurveSettingsLoaded=storage.loaded;
        if(result==windows::FanCurveLoadResult::NoPath)localCurveStatus=L"Портативная папка MagicControl не найдена; локальный профиль не загружен.";
        else if(result==windows::FanCurveLoadResult::Loaded)localCurveStatus=L"Настройки из папки MagicControl загружены; EC не изменён.";
        else if(result==windows::FanCurveLoadResult::Failed)localCurveStatus=L"Файл локальных кривых не прочитан: "+wide(profileError);
        else if(result==windows::FanCurveLoadResult::L16Created)localCurveStatus=L"Создан отдельный профиль кривых L16; EC не изменён.";
    }
bool FansOperationsWindows::saveLocalCurveSettings(){
        windows::FanCurveStorage storage{localCurveSettingsPath,profileError,localCurveSettingsLoaded};
        const bool saved=windows::saveLocalFanCurves(preview,profiles,storage);
        profileError=std::move(storage.error);localCurveSettingsLoaded=storage.loaded;
        return saved;
    }
void FansOperationsWindows::saveLocalCurveEdits(){
        if(preview)return;
        view_.cancelProfileSave();
        if(nativeCurves){if(!busy&&hlylController&&hlylKnown)try{FanWriteRequest command{FanResourceOperation::SaveDraft};command.profiles=profiles;executeFanResource({nullptr,hlylController.get(),nullptr,{},nativeCurves,deviceProfile(resourceBoard).id},command).requireAccepted();operationStatus=L"Кривая сохранена. Нажмите Manual для применения.";}catch(const std::exception& e){operationStatus=wide(e.what());}return;}
        if(huawei)return; // HLYL fixed PWM is not an autonomous EC curve CSV.
        if (saveLocalCurveSettings()) {
            localCurveStatus = profiles.ecManual ? L"Кривая Мануал сохранена. Для применения нажмите Мануал." : L"Изменения сохранены. Для применения правок нажмите Мануал онлайн.";
            operationStatus = localCurveStatus;
        } else {
            operationStatus = L"Настройки не сохранены: " + wide(profileError);
        }
    }
void FansOperationsWindows::highlightRequestedMode(){highlightBaselineRevision=fanController?fanController->status().revision:0;requestedHighlight=profiles;view_.invalidate(Presets|Manual|Link|Immediate);}
void FansOperationsWindows::adoptCustomCurves(const mc::gm6::Curves& curves){
        profiles.ecManual=false;profiles.linked=curves[0]==curves[1];profiles.selected.fill(mc::FanCurvePreset::Manual);
        for(int ch=0;ch<2;++ch)for(int i=0;i<16;++i)profiles.curves[static_cast<std::size_t>(mc::FanCurvePreset::Manual)][ch][i]={curves[ch][i].up,curves[ch][i].duty};
        for(int i=0;i<16;++i)profiles.ecManualCurve[i]={curves[0][i].up,curves[0][i].duty};
        localCurveSettingsLoaded=true;gm6DraftKnown=true;
        view_.updateEditorText(0);view_.updateEditorText(1);draftEdited=true;saveLocalCurveEdits();
    }
void FansOperationsWindows::observeGm(const mc::windows::Gm6State& value){
        if(!preview&&value.selectedCustomCurves&&value.customPresetRevision&&value.customPresetRevision!=seenCustomPresetRevision){
            seenCustomPresetRevision=value.customPresetRevision;adoptCustomCurves(*value.selectedCustomCurves);
        }
        gm6State=value;modeKnown=value.connected;nativeActive=value.custom&&value.ramActive;
        nativeDefaultMode=!value.custom&&!value.boost&&!value.overrideActive&&value.nativeProfile.has_value();
        nativeFanBoost=value.boost;sharedMode=!value.respective;
        if(value.tablesRead)gm6TablesKnown=true;
        if(value.curves)for(int ch=0;ch<2;++ch){auto& table=ch?appliedGpu:appliedCpu;for(int i=0;i<16;++i)table[i]={(*value.curves)[ch][i].up,(*value.curves)[ch][i].duty};}
        // Observed hardware tables are applied state only. They never replace
        // the CSV editor or add an EC marker to its logical first point.
    }
void FansOperationsWindows::applyGm(int mode,int onlineChannel){
        audit::Scope dispatch("fans.gm_dispatch","\"mode\":"+std::to_string(mode));
        if(const auto reason=windows::gmFanAdmissionReason(preview,busy,bool(gm6Controller),gm6State.connected,gm6State.writable)){dispatch.finish("rejected","\"reason\":"+audit::quote(reason));return;}
        auto operation=mc::deviceOperations().tryBegin();if(!operation){operationStatus=L"Дождитесь завершения текущей аппаратной операции.";return;}
        view_.commitEditors();const auto curves=mc::customCurvesFromFanProfiles(profiles);
        auto controlled=windows::fanOnlineControlled(gm6State.online,gm6State.onlineControlled,onlineChannel,profiles.linked,mode);
        const bool linked=profiles.linked;
        const auto generation=++typedStateGeneration;
        beginFanNotice(mode);
        busy=true;gm6Requested=mode;gm6RequestedChannels=controlled;view_.positionControls();view_.invalidate(View);
        FanRequest request;request.writing=true;request.command.operation=gmFanOperation(mode);request.command.curves=curves;request.command.controlled=controlled;request.command.linked=linked;
        request.generation=generation;request.reservation=std::move(operation);launchCommand(std::move(request));
        dispatch.finish("queued");
    }
void FansOperationsWindows::applyHlyl(int mode){
        audit::Scope dispatch(nativeCurves?"fans.hpbsd_dispatch":"fans.huawei_dispatch","\"mode\":"+std::to_string(mode));
        if(const auto reason=windows::nativeFanAdmissionReason(busy,bool(hlylController),hlylKnown,hlylState.writable)){dispatch.finish("rejected","\"reason\":"+audit::quote(reason));return;}
        auto operation=mc::deviceOperations().tryBegin();
        if(!operation){operationStatus=L"Дождитесь завершения текущей аппаратной операции.";return;}
        if(mode!=0&&hlylState.needsRestore)return;
        if(nativeCurves)ensureNativeEditorDraft();
        if(nativeCurves&&mode==1&&!hlylState.curves)return;
        view_.commitEditors();const auto target=windows::nativeFanTarget(mode,hlylState.minimumPercent,hlylDraft);
        const auto generation=++typedStateGeneration;
        beginFanNotice(mode);
        busy=true;hlylRequested=mode;operationStatus=nativeCurves?L"HPBSD: применяю режим вентиляторов…":L"Honor: применяю режим вентиляторов…";
        view_.positionControls();view_.invalidate(View);
        FanRequest request;request.writing=true;request.command.operation=nativeFanOperation(mode);request.command.target=target;if(nativeCurves&&mode==1)request.command.profiles=profiles;
        request.generation=generation;request.reservation=std::move(operation);launchCommand(std::move(request));
        dispatch.finish("queued");
    }
void FansOperationsWindows::selectEcManual(){
        if(preview){view_.commitEditors();profiles.selected.fill(FanCurvePreset::Manual);profiles.linked=true;profiles.ecManual=true;
            operationStatus=L"Просмотр профиля · аппаратное управление отключено";view_.updateEditorText(0);view_.updateEditorText(1);view_.positionControls();view_.invalidate(View|Presets|Manual|Link);return;}
        if(huawei){applyHlyl(1);return;}
        if (busy || mc::deviceOperations().busy()) return;
        view_.commitEditors();
        try {
            if (!fanController&&!gm6Controller) throw std::runtime_error("EC_manual: нет проверенного доступа к EC.");
            const auto cpuTable = profiles.ecManualCurve;
            // Verified autonomous EC mode uses one common CPU curve.
            profiles.selected.fill(mc::FanCurvePreset::Manual); profiles.linked = true; profiles.ecManual = true;
            highlightRequestedMode();
            if(gm6Controller){
                view_.updateEditorText(0);view_.updateEditorText(1);applyGm(1);
                draftEdited=true;saveLocalCurveEdits();return;
            }
            beginFanNotice(1);
            FanWriteRequest request{FanResourceOperation::Manual};request.ecCurve=cpuTable;executeFanResource({nullptr,nullptr,fanController.get()},request).requireAccepted();
            draftEdited = true; saveLocalCurveEdits();
            view_.updateEditorText(0); view_.updateEditorText(1);
            operationStatus = L"EC_manual: CPU=GPU, записываю общую кривую CPU…";
        } catch (const std::exception& error) { requestedHighlight.reset(); operationStatus = wide(error.what());finishFanNotice(false,operationStatus); }
        view_.positionControls(); view_.invalidate(View);
    }
void FansOperationsWindows::selectPreset(int channel, mc::FanCurvePreset preset){
        if(preview){if(channel<0||channel>1)return;view_.commitEditors();windows::selectFanPresetIntent(profiles,channel,preset);
            operationStatus=L"Просмотр профиля · аппаратное управление отключено";view_.resetCurvePage(channel);view_.updateEditorText(0);view_.updateEditorText(1);view_.positionControls();view_.invalidate(View|Presets|Manual|Link);return;}
        if(huawei){if(const auto mode=windows::nativeFanPresetMode(preset,nativeCurves))applyHlyl(*mode);return;}
        if (busy || mc::deviceOperations().busy()) { operationStatus = L"Ожидайте завершения текущего переключения EC."; return; }
        if(!gm6Controller&&!fanController){operationStatus=L"Аппаратное управление недоступно; локальная кривая не применена.";return;}
        if (fanController && fanController->status().needsRestore && preset != mc::FanCurvePreset::Default) {
            operationStatus = L"Сначала завершите возврат штатного режима через Default."; return;
        }
        if (busy) return;
        view_.commitEditors();
        windows::selectFanPresetIntent(profiles,channel,preset);
        highlightRequestedMode();
        draftEdited = true;
        view_.resetCurvePage(channel);
        saveLocalCurveEdits();
        view_.updateEditorText(0);
        view_.updateEditorText(1);
        try {
            if(gm6Controller){
                if(preset==FanCurvePreset::Default)applyGm(0);
                else if(preset==FanCurvePreset::Max)applyGm(2);
                else if(preset==FanCurvePreset::Manual)applyGm(3,channel);
                return;
            }
            if (!fanController) throw std::runtime_error("Управление EC недоступно: нужен проверенный драйвер и права администратора.");
            beginFanNotice(preset==FanCurvePreset::Default?0:preset==FanCurvePreset::Max?2:3);
            FanWriteRequest request{preset==FanCurvePreset::Default?FanResourceOperation::Default:preset==FanCurvePreset::Max?FanResourceOperation::Maximum:FanResourceOperation::Restore};request.profiles=profiles;executeFanResource({nullptr,nullptr,fanController.get()},request).requireAccepted();
            operationStatus = preset == mc::FanCurvePreset::Default ? L"Возвращаю штатный режим CPU/GPU…" :
                preset == mc::FanCurvePreset::Max ? L"Включаю Fan Boost CPU/GPU…" : L"Применяю кривую…";
        } catch (const std::exception& error) { requestedHighlight.reset(); operationStatus = wide(error.what());finishFanNotice(false,operationStatus); }
        view_.positionControls();
        view_.invalidate(View);
    }
void FansOperationsWindows::readNativeResult(JobResult& result) const{
        const auto state = windows::readNativeFanTables(*ec,profiles.ecManual);
        result.cpu = state.cpu; result.gpu = state.gpu;
        result.shared = state.shared; result.active = state.active; result.defaultMode = state.defaultMode; result.boost = state.boost;
    }
void FansOperationsWindows::readCurves(){
        if(preview){curvesLoaded=true;operationStatus=L"Просмотр профиля · аппаратное управление отключено";return;}
        if(gm6Controller){curvesLoaded=true;operationStatus=L"GM6IR0C: читаю фактическое состояние EC…";return;}
        if(huawei){curvesLoaded=true;operationStatus=L"Honor: читаю фактический PWM IT8987…";return;}
        if (!ec || busy) {
            curvesLoaded = ec == nullptr;
            operationStatus = ec ? L"Подключаюсь к таблицам EC…"
                : localCurveSettingsLoaded
                    ? L"Локальные кривые загружены; EC недоступен, значения не применены."
                    : L"EC недоступен. Данные не читаются.";
            if(deviceProfile(resourceBoard).id=="eluktronics-hydroc16g2")
                operationStatus=L"HYDROC: профиль диагностики. Полный снимок EC — INFO → «Снять дамп».";
            return;
        }
        busy = true;
        operationStatus = L"Читаю кривые и режим EC…";
        view_.positionControls();
        view_.invalidate(View);
        launchCommand(FanRequest{});
    }
void FansOperationsWindows::toggleCurveLink(){
        if(preview){view_.commitEditors();profiles.linked=!profiles.linked;operationStatus=L"Просмотр профиля · аппаратное управление отключено";view_.updateEditorText(0);view_.updateEditorText(1);view_.positionControls();view_.invalidate(View|Presets|Manual|Link);return;}
        if(gm6Controller){if(busy||!gm6State.online)return;view_.commitEditors();const bool before=windows::toggleGmFanLinkIntent(profiles);view_.updateEditorText(0);view_.updateEditorText(1);applyGm(3);if(!busy)profiles.linked=before;else saveLocalCurveEdits();view_.updateEditorText(0);view_.updateEditorText(1);return;}
        if (!fanController) return;
        const auto mode = fanController->status();
        if (!mode.owned || mode.ecManual || mode.maximum || mode.error ||
            (!mode.controlled[0] && !mode.controlled[1])) return;
        if (mc::deviceOperations().busy()) { operationStatus = L"Ожидайте завершения текущего переключения EC."; return; }
        if (fanController && fanController->status().needsRestore) { operationStatus = L"Сначала завершите возврат штатного режима через Default."; return; }
        if (busy) return;
        view_.commitEditors();
        windows::toggleOnlineFanLinkIntent(profiles,mode);
        highlightRequestedMode();
        draftEdited = true;
        saveLocalCurveEdits();
        view_.updateEditorText(0);
        view_.updateEditorText(1);
        try {
            if (!fanController) throw std::runtime_error("Управление EC недоступно: драйвер или плата не проверены.");
            beginFanNotice(3);
            FanWriteRequest request{FanResourceOperation::Restore};request.profiles=profiles;executeFanResource({nullptr,nullptr,fanController.get()},request).requireAccepted();
            operationStatus = profiles.linked ? L"Включаю CPU=GPU…" : L"Включаю CPU≠GPU…";
        } catch (const std::exception& error) { requestedHighlight.reset(); operationStatus = wide(error.what());finishFanNotice(false,operationStatus); }
        view_.positionControls();
        view_.invalidate(View);
    }
void FansOperationsWindows::completeWorker(){
        if(!commandWorker.takeCompletion())return;
        auto result=std::move(pendingJob);pendingJob.reset();
        if(commandWorker.failure()){JobResult failed;failed.typedGeneration=typedStateGeneration;failed.message=L"Ошибка операции FANs; состояние не подтверждено.";result=std::move(failed);}
        if((gm6Controller||huawei)&&result&&result->typedGeneration!=typedStateGeneration){
            // The result belongs to an older request. Preserve the current
            // requested/actual state and let its own completion publish it.
            if(fanNoticeToken&&fanNoticeGeneration==result->typedGeneration)finishFanNotice(false,L"Профиль изменён; результат прежней команды не применяется.");
            return;
        }
        if (controlStatus.revision) operationStatus = wide(controlStatus.message);
        busy = false;
        if((gm6Controller||huawei)&&result&&fanNoticeToken)finishFanNotice(result->success,result->message);
        if(gm6Controller){gm6Requested.reset();if(result&&result->gm6){if(!result->success&&result->gm6->online)profiles.linked=result->gm6->onlineLinked;observeGm(*result->gm6);}
            if(result)operationStatus=result->message;
            view_.positionControls();view_.invalidate(View);view_.notifyOwner();return;}
        if(huawei){
            hlylRequested.reset();hlylKnown=result&&result->hlyl.has_value();if(hlylKnown){hlylState=*result->hlyl;modeKnown=true;}
            if(result)operationStatus=result->message;
            view_.positionControls();view_.invalidate(Presets|Manual);view_.invalidate(View);view_.notifyOwner();return;
        }
        if (result) {
            operationStatus = result->message;
            if (result->success) {
                appliedCpu = result->cpu; appliedGpu = result->gpu;
                nativeActive = result->active; nativeDefaultMode = result->defaultMode; nativeFanBoost = result->boost;
                modeKnown = true;
                if (!localCurveSettingsLoaded && !draftEdited) {
                    profiles.edit(0, result->cpu);
                    profiles.edit(1, result->gpu);
                } else {
                    operationStatus = curveApplied(0) && curveApplied(1)
                        ? L"Выбранные профили совпадают с прочитанным состоянием EC."
                        : L"Локальные кривые загружены. В EC действует другой профиль; настройки не применены.";
                }
                sharedMode = result->shared;
                curvesLoaded = true;
                view_.clampCurvePages();
                view_.updateEditorText(0);
                view_.updateEditorText(1);
            } else {
                curvesLoaded = false;
                modeKnown = false;
            }
        }
        view_.positionControls();
        view_.invalidate(View);
        view_.invalidate(Page);
        view_.notifyOwner();
    }
void FansOperationsWindows::syncControlStatus(){
        if (!fanController) return;
        auto next = fanController->status();
        if (requestedHighlight && (next.error || next.needsRestore ||
            (!next.applying && !next.busy && next.revision > highlightBaselineRevision)))
            requestedHighlight.reset();
        if (next.revision && !next.applying && !next.suspended && !next.error) {
            modeKnown = true;
            nativeFanBoost = next.maximum;
            nativeActive = !next.maximum && (next.controlled[0] || next.controlled[1]);
            nativeDefaultMode = !nativeFanBoost && !nativeActive;
            sharedMode = next.ecManual || (!nativeActive && (next.maximum || next.linked));
        }
        const bool changed = next.message != lastControlMessage;
        controlStatus = std::move(next);
        if(fanNoticeToken&&!gm6Controller&&!huawei&&controlStatus.revision>fanNoticeRevision&&
           !controlStatus.applying&&!controlStatus.busy)
            finishFanNotice(!controlStatus.error&&!controlStatus.needsRestore&&!controlStatus.suspended,wide(controlStatus.message));
        view_.updateAvailability();
        // Owner-drawn child buttons need their own invalidation when the
        // asynchronous apply result changes; repainting the parent is insufficient.
        view_.invalidate(Presets|Manual|Link);
        if (visible) view_.invalidate(View);
        if (changed && !controlStatus.message.empty()) {
            lastControlMessage = controlStatus.message;
            operationStatus = wide(controlStatus.message);
            if (telemetryLog) telemetryLog("FAN CONTROL: " + controlStatus.message);
            if (visible) view_.invalidate(View);
        }
    }
void FansOperationsWindows::pollSensors(){
        if(preview)return;
        syncControlStatus();
        if ((!visible && !anyLoggingEnabled()) || sensorWorker.busy() || busy || (ec && ec->observationsPaused())) return;
        if (!visible && !batteryLoggingEnabled() && !windows::powerDiagnosticBurstActive() && GetTickCount64() < nextBackgroundPoll) return;
        nextBackgroundPoll = GetTickCount64() + 5000;
        const bool readGmTables=!gm6TablesKnown;const auto generation=typedStateGeneration;
        try{sensorWorker.launch([this,readGmTables,generation]{pendingSensors=readFanMonitoring({ec,*monitoringReader,monitoringSource,hlylController.get(),gm6Controller.get(),deviceProfile(resourceBoard).id=="minisforum-hpbsd",readGmTables,generation});},[this]{if(ready_)ready_(true);});}
        catch(const std::exception& error){operationStatus=wide(error.what());if(telemetryLog)telemetryLog("FANs observation start failed: "+std::string(error.what()));if(visible)view_.invalidate(View);}
    }
void FansOperationsWindows::completeSensors(){
        if(!sensorWorker.takeCompletion())return;
        const int previousNativeMode=hlylState.mode;
        {
            if(pendingSensors&&(gm6Controller||huawei)&&pendingSensors->typedGeneration!=typedStateGeneration)
                pendingSensors.reset(); // Older typed mode/PWM observations are not new readback.
            if (pendingSensors) {
                if(pendingSensors->gm6){observeGm(*pendingSensors->gm6);if(!busy)operationStatus=wide(pendingSensors->gm6->status);}
                if(huawei&&!pendingSensors->typedObservationSkipped){hlylKnown=pendingSensors->hlyl.has_value();if(hlylKnown){hlylState=*pendingSensors->hlyl;modeKnown=true;if(!busy)operationStatus=hlylState.status.empty()?(hlylState.manual?L"Honor: ручной PWM · фактическое состояние EC.":L"Honor: штатная автоматика; Manual задаёт фиксированный PWM."):wide(hlylState.status);}}
                cpu = pendingSensors->cpu; gpu = pendingSensors->gpu;
                monitoring = displayCache.update(std::move(pendingSensors->monitoring));
                // Physical fan channels are accepted once with their current
                // availability; the generic display cache must not supply a
                // second fan value behind the channel's own readout.
                for(unsigned channel=0;channel<2;++channel){const auto& fan=channel==0?cpu:gpu;
                    monitoring.processors[channel].fanRpm=fan.rpmAvailable?std::optional<double>(fan.rpm):std::nullopt;
                    monitoring.processors[channel].fanPwm=fan.pwmAvailable?std::optional<double>(fan.dutyPercent):std::nullopt;
                }
                resourceState.update(resourceBoard,monitoring);
                const ResourcePresentationView resources(resourceState);
                const auto cpuFanProjection=mc::monitoringFanReading(monitoring,0,&resources);
                const auto gpuFanProjection=mc::monitoringFanReading(monitoring,1,&resources);
                auto presentation=audit::presentation(monitoring);presentation.pop_back();
                presentation+=",\"resource_state\":"+audit::resourceStateSummary(resourceState)+
                    ",\"resource_state_accepted\":"+(resources.available()?"true":"false")+
                    ",\"fan_resource_used\":[{\"speed\":"+(cpuFanProjection.rpmAvailable?"true":"false")+",\"duty\":"+(cpuFanProjection.pwmAvailable?"true":"false")+"},{\"speed\":"+(gpuFanProjection.rpmAvailable?"true":"false")+",\"duty\":"+(gpuFanProjection.pwmAvailable?"true":"false")+"}]}";
                audit::state("FANs.presentation",presentation,this);
                ecTemperature = pendingSensors->ecTemperature;
                pendingSensors.reset();
            }
        }
        if(nativeCurves){if(previousNativeMode!=hlylState.mode)view_.positionControls();else{view_.updateEditorText(0);view_.updateEditorText(1);}}
        if(huawei||gm6Controller){view_.updateAvailability();view_.invalidate(Presets|Manual);}
        if (telemetryLog && !processorModelsLogged) {
            telemetryLog("FANs startup models: CPU=" + monitoring.processorModels[0] + " GPU=" + monitoring.processorModels[1]);
            processorModelsLogged = true;
        }
        view_.monitoringLayoutChanged();
        if (telemetryLog && monitoring.processorStatus != lastProcessorStatus) {
            const auto& p = monitoring.processors;
            telemetryLog(std::format("FANs telemetry: CPU={}C source={} Tctl={}C PMLog={}C ADL-sensor={} adapter={} GPU={}C source={} ADL-sensor={} adapter={} SoC={}C NPU={}% source={} NPU-power={}W source={} APU={}W source={} DDR5={}/{}C EC-diagnostic={}C status={}",
                audit::metric(p[0].celsius,1,true), p[0].temperatureSource,
                audit::metric(p[0].tctlCelsius,3,true), audit::metric(p[0].driverCelsius,1,true), p[0].temperatureSensorId,p[0].adapterIndex,
                audit::metric(p[1].celsius,1,true), p[1].temperatureSource, p[1].temperatureSensorId,p[1].adapterIndex,
                audit::metric(monitoring.socCelsius,1,true),audit::metric(monitoring.npuUsagePercent),monitoring.npuUsageSource,
                audit::metric(monitoring.npuPowerWatts,2),monitoring.npuPowerSource,
                audit::metric(p[0].powerWatts,2),p[0].powerSource,audit::metric(monitoring.memory[0].celsius,1),
                audit::metric(monitoring.memory[1].celsius,1),ecTemperature?std::to_string(*ecTemperature):"unknown",monitoring.processorStatus));
            lastProcessorStatus = monitoring.processorStatus;
        }
        if (cpu.rpmAvailable || cpu.pwmAvailable || cpu.temperatureAvailable ||
            gpu.rpmAvailable || gpu.pwmAvailable || gpu.temperatureAvailable) {
            ++pollCount;
        }
        if (visible) view_.invalidate(View);
        view_.notifyOwner();
    }
bool FansOperationsWindows::curveApplied(int channel) const{
        if(nativeCurves){if(!hlylKnown||!hlylState.curves||hlylState.needsRestore)return false;if(hlylState.mode!=1)return hlylState.mode>=0;
            if(!hlylState.curves->appliedCurve)return false;
            if(hlylState.sharedCurve)return profiles.curves[0]==hlylState.curves->draft;
            try{const auto s=mc::hpbsd::withValues(static_cast<mc::hpbsd::Byte>(hlylState.curves->selector),mc::hpbsd::curve(profiles.curves[0],static_cast<mc::hpbsd::Byte>(hlylState.curves->selector)));const auto p=mc::hpbsd::points(s);if(!std::equal(p[channel].begin(),p[channel].begin()+profiles.activePointCount(channel),hlylState.curves->actual[channel].begin()))return false;for(int i=0;i<profiles.activePointCount(channel);++i)if(s[(channel?44:27)+i]!=hlylState.curves->slopes[channel][i])return false;return true;}catch(...){return false;}
        }
        if(gm6State.online){
            if(!gm6State.onlineControlled[channel]||!gm6State.onlineCurves)return false;
            const auto& points=(*gm6State.onlineCurves)[gm6State.onlineLinked?0:channel];const auto& draft=profiles.current(channel);
            for(int i=0;i<16;++i)if(points[i].up!=draft[i].temperature||points[i].duty!=draft[i].duty)return false;return true;
        }
        if(gm6Controller){
            if(!gm6State.connected||gm6State.boost||!gm6State.custom||!gm6State.ramActive||!gm6State.curves)return false;
            const auto& actual=(*gm6State.curves)[channel];const auto& draft=profiles.current(channel);
            // Raw GM6 RAM omits the logical first anchor temperature. Compare
            // its duty and every encoded threshold, keeping the CSV unchanged.
            for(int i=0;i<16;++i)if((i&&actual[i].up!=draft[i].temperature)||actual[i].duty!=draft[i].duty)return false;
            return true;
        }
        if(huawei)return hlylKnown&&hlylState.manual&&(nativeCurves?!hlylState.needsRestore:hlylState.actualPercent[channel]==hlylState.targetPercent[channel]);
        if (controlStatus.owned && controlStatus.controlled[channel]) {
            if (profiles.currentPreset(channel) == mc::FanCurvePreset::Default) return false;
            if (profiles.currentPreset(channel) == mc::FanCurvePreset::Max)
                return controlStatus.maximum && controlStatus.confirmed[channel];
            return !controlStatus.maximum && controlStatus.confirmed[channel] &&
                controlStatus.applied[channel] == profiles.current(channel) && controlStatus.linked == profiles.linked;
        }
        if (modeKnown && nativeFanBoost && profiles.currentPreset(channel) == mc::FanCurvePreset::Max) return true;
        return modeKnown && (profiles.currentPreset(channel) == mc::FanCurvePreset::Default
            ? nativeDefaultMode : nativeActive && profiles.current(channel) == (channel == 0 ? appliedCpu : appliedGpu));
    }
int FansOperationsWindows::definedCount(int channel) const{
        const auto& curve = profiles.current(channel);
        return static_cast<int>(std::count_if(curve.begin(), curve.begin()+profiles.activePointCount(channel),
            [](const CurveCell& cell) { return cell.temperature != 255; }));
    }
void FansOperationsWindows::initialize(){if(!preview&&!fanController&&ec&&ec->boardVerified()&&ec->info().writable)fanController=std::make_shared<FanController>(*ec);readCurves();}
void FansOperationsWindows::refreshDeviceState(){
    if(!gm6Controller)return;
    ++typedStateGeneration;busy=false;gm6Requested.reset();gm6TablesKnown=false;nextBackgroundPoll=0;pollSensors();
}
void FansOperationsWindows::shutdown(){if(stopped_)return;stopped_=true;if(!preview&&!huawei&&!gm6Controller&&draftEdited)saveLocalCurveSettings();sensorWorker.join();commandWorker.join();fanController.reset();hlylController.reset();}
void FansOperationsWindows::bindReady(std::function<void(bool)> ready){ready_=std::move(ready);}
void FansOperationsWindows::syncCustomCurve(){
        if(!preview&&fanController)try{
            const auto id=deviceProfile(resourceBoard).id;
            if(windows::syncConfirmedCustomFanCurve(id,*fanController,seenCustomPresetFile,profiles)){
                view_.updateEditorText(0);view_.updateEditorText(1);draftEdited=true;saveLocalCurveEdits();
                if(profileError.empty())operationStatus=L"Custom: кривая Manual подтверждена чтением EC.";
            }
        }catch(const std::exception& error){if(telemetryLog)telemetryLog(std::string("Custom curve editor sync: ")+error.what());}
    }
void FansOperationsWindows::beginFanNotice(int mode){
        fanNotice=mc::gui::osd::fanMode(mode==0?"Default":mode==2?"Max":mode==3?(nativeCurves?"Min":"Manual online"):"Manual");
        fanNoticeRevision=fanController?fanController->status().revision:0;fanNoticeGeneration=typedStateGeneration;fanNoticeToken=view_.beginNotice(fanNotice);
    }
void FansOperationsWindows::finishFanNotice(bool success,const std::wstring& detail){if(fanNoticeToken){auto value=fanNotice;value.detail=mc::windows::encoding::utf8(detail);value.error=!success;view_.completeNotice(fanNoticeToken,std::move(value));}fanNoticeToken=0;}
void FansOperationsWindows::launchCommand(FanRequest request){
        commandWorker.join(); // Preserve superseded-generation admission order.
        const auto workClass=request.writing?ResourceWorkClass::Interactive:ResourceWorkClass::Observation;
        try{commandWorker.launch([this,request=std::move(request)]{
            JobResult result;result.typedGeneration=request.generation;
            if(request.writing){
                auto value=executeFanResource({gm6Controller.get(),hlylController.get(),nullptr,request.reservation,nativeCurves,deviceProfile(resourceBoard).id},request.command);
                result.success=value.success;result.message=std::move(value.message);result.gm6=std::move(value.gm6);result.hlyl=std::move(value.native);
            }
            else{for(auto& cell:result.cpu)cell={255,100};for(auto& cell:result.gpu)cell={255,100};try{readNativeResult(result);result.success=true;result.message=L"Кривые прочитаны из EC.";}catch(const std::exception& e){result.message=L"Не удалось прочитать таблицы EC: "+wide(e.what());}catch(...){result.message=L"Не удалось прочитать таблицы EC.";}}
            pendingJob=std::move(result);
        },[this]{if(ready_)ready_(false);},workClass);}
        catch(const std::exception& e){busy=false;gm6Requested.reset();hlylRequested.reset();operationStatus=wide(e.what());finishFanNotice(false,operationStatus);view_.positionControls();view_.invalidate(View);}
    }
bool FansOperationsWindows::fanNeedsRestore()const{return fanController&&fanController->status().needsRestore;}
bool FansOperationsWindows::fanObservedDefault()const{return fanController&&fanController->status().observedDefault==true;}
void FansOperationsWindows::configurePreviewMonitoring(MonitoringSnapshot seed,bool providePreview){
    if(!preview)return;
    monitoringSource=[seed=std::move(seed)]{return seed;};
    if(providePreview){const auto value=monitoringSource();monitoring.processorVendors=value.processorVendors;monitoring.processorModels=value.processorModels;}
}
FansOperationsWindows::CurveTable FansOperationsWindows::normalizedEditorCurve(int channel){
    auto curve=profiles.current(channel);
    if(profiles.currentPreset(channel)!=FanCurvePreset::Default)fanInput::normalizeDraft(curve);
    if(profiles.ecManual)profiles.ecManualCurve=curve;
    else if(profiles.currentPreset(channel)==FanCurvePreset::Manual)profiles.curves[static_cast<std::size_t>(FanCurvePreset::Manual)][profiles.linked?0:channel]=curve;
    return curve;
}
void FansOperationsWindows::ensureNativeEditorDraft(){
    // Follow a NEW owner-confirmed draft (including a Custom preset). Repeated
    // observations never overwrite unsaved local input or adopt factory data.
    if(nativeCurves&&hlylKnown&&hlylState.curves&&(!nativeObservedDraft||*nativeObservedDraft!=hlylState.curves->draft)){
        nativeObservedDraft=hlylState.curves->draft;profiles.curves[0]=*nativeObservedDraft;profiles.linked=hlylState.sharedCurve;profiles.ecManual=false;
    }
}
void FansOperationsWindows::editNativeBand(int channel,int band,int temperature,int duty){
    if(!nativeCurves||hlylState.mode!=1||channel<0||channel>=2||band<0||band>=(hlylState.editablePointCounts[channel]?hlylState.editablePointCounts[channel]:profiles.activePointCount(channel)-1))return;
    auto& point=profiles.curves[0][channel][band];point.temperature=fanInput::temperature(temperature);point.duty=fanInput::percent(duty);
    if(hlylState.sharedCurve)profiles.curves[0][1-channel][band]=point;
}
void FansOperationsWindows::editFixedTarget(int channel,int value){if(huawei&&channel>=0&&channel<2)hlylDraft[channel]=fanInput::percent(value);}

}
