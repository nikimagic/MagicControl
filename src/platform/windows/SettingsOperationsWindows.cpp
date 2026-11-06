#include "SettingsOperationsWindows.h"
#include "FanControlWindows.h"
#include "SettingsSnapshotWindows.h"
#include "../../devices/minisforum/hpbsd/PowerControlWindows.h"
#include "PowerDiagnosticsWindows.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/ControlAudit.h"
#include "MagicControl/Logging.h"
#include "MagicControl/RomIdHistory.h"
#include <windows.h>
#include <stdexcept>
#include <utility>

namespace mc::gui {
void executeAuditedSettingsWorker(const SettingsWorkerAuditContext& c,
    const std::function<std::wstring(const SettingsFailureMarkers&)>& execute){
    IdentityHistoryScope history(c.saveChoice?"user":"observation",c.saveChoice?"settings.apply":"settings.poll");
    audit::Scope trace("settings.apply","\"control_id\":"+std::to_string(c.command)+",\"cached_before\":"+settingsSnapshotAudit(c.previous),c.auditId,c.saveChoice);
    const SettingsFailureMarkers markers{[&](const char* message){history.fail(message);},[&](const char* message){trace.fail(message);}};
    auto result=execute(markers);
    audit::state("windows.settings",settingsSnapshotAudit(c.pending));
    if(c.saveChoice)trace.finish(c.pendingSuccess?"completed_with_observation":"failed",
        "\"message\":"+audit::quote(c.encode(result))+",\"observed_state\":"+settingsSnapshotAudit(c.pending));
    c.publish(std::move(result));
}
void executeSettingsExecution(SettingsExecutionContext& c){
    const auto& f=c.flags;
    if(f.keyboardIdleOnly){
        c.pending=c.previous;c.pending.keyboardIdle.reset();
        try{c.pending.keyboardIdle=collectSettingsKeyboardIdle(c.snapshot());c.pendingSuccess=true;}
        catch(const std::exception& error){if(c.log&&c.lastReadError!=error.what())c.log(std::string("Keyboard idle read: ")+error.what());c.lastReadError=error.what();}
        // No actions, restore, settings persistence or supplemental SMU reads.
        c.publish({});return;
    }
    executeAuditedSettingsWorker({c.previous,c.pending,c.command,c.auditId,f.saveChoice,c.pendingSuccess,c.encode,c.publish},
        [&](const SettingsFailureMarkers& markers){
            SettingsWorkerContext work{c.previous,c.pending,c.snapshot,c.expectedPower,f.quickBattery,f.gm5Ui,f.saveChoice,
                c.profile,c.action,c.persist,
                [&](std::uint64_t started,std::uint64_t appliedAt){
                    const auto s=c.snapshot();
                    return SettingsSupplementalContext{c.previous,f.gm6NativePower,f.quickBattery,f.quickProfile,f.saveChoice,
                        f.intelCpuUi,f.gm5Ui,c.command,c.batteryVoltageMode,started,appliedAt,std::string(c.profile()),
                        s.ec,s.laptop,s.gm,c.nativeBattery,c.voltageBattery,c.batteryReader,c.intelMonitor,c.apuPowerReader,
                        c.priorityPending,c.log,c.lastApuLog};
                },
                [&]{const auto s=c.snapshot();return SettingsResultContext{f.saveChoice,bool(s.ec),bool(s.laptop),bool(s.gm),
                    bool(c.nativeBattery),bool(s.hid),bool(s.cycle),f.batteryUpper,f.nativeMode,f.batteryPolicy,
                    f.cycleTrigger,f.colorChoice,f.lightingChoice};},
                c.initialMessage,
                [&]{const auto s=c.snapshot();return SettingsFailureContext{c.reading,bool(s.ec||s.laptop||s.gm||c.nativeBattery),
                    c.lastReadError,markers.history,markers.audit,
                    [](const char* message){windows::recordPowerAction("settings_operation","failed",-1,message);},
                    c.log,c.toWide,[&]{return recoverSettingsObservation(c.pending,c.snapshot(),c.nativeBattery,c.batteryReader,c.apuPowerReader);}};},
                c.pendingSuccess,c.clearActual,c.lastReadError,c.toWide};
            return executeSettingsWorker(work);
        });
}
SettingsFailureResult processSettingsFailure(const std::exception& error,const SettingsFailureContext& c){
    // Diagnostic sinks must not prevent failure reporting or recovery.
    try{if(c.failHistory)c.failHistory(error.what());}catch(...){}
    try{if(c.failAudit)c.failAudit(error.what());}catch(...){}
    try{if(c.log&&(!c.reading||c.lastReadError!=error.what()))c.log(std::string("SETs operation failed: ")+error.what());}catch(...){}
    try{if(!c.reading&&c.recordFailure)c.recordFailure(error.what());}catch(...){}
    c.lastReadError=error.what();
    SettingsFailureResult result;
    try{result.message=L"Ошибка: "+c.toWide(error.what());}
    catch(...){result.message=L"Ошибка SETs: сообщение недоступно.";}
    if(c.hasRecoveryBackend&&!c.reading){
        try{if(!c.recover||!c.recover())result.clearActual=true;}
        catch(...){result.clearActual=true;}
    }
    return result;
}
std::wstring executeSettingsOperation(const std::function<std::wstring()>& work,
    const std::function<SettingsFailureContext()>& failureContext,bool& clearActual){
    const auto fail=[&](const std::exception& error){
        try{const auto failure=processSettingsFailure(error,failureContext());
            if(failure.clearActual)clearActual=true;
            return failure.message;
        }catch(...){clearActual=true;return std::wstring(L"Ошибка SETs: состояние не подтверждено.");}
    };
    try{return work();}
    catch(const std::exception& error){return fail(error);}
    catch(...){return fail(std::runtime_error("unknown SETs operation exception"));}
}
bool resolveSettingsObservationRevision(SettingsSnapshot& pending,bool& clearActual,std::wstring& message,
    const SettingsObservationRevision& revision) noexcept{
    const bool superseded=revision.nativeAtStart!=revision.nativeCurrent;
    if(superseded){pending.valid=false;clearActual=false;message.clear();}
    if(pending.valid&&revision.passive&&revision.observationAtStart!=revision.observationCurrent){
        if(revision.previousValid)pending.valid=false;
        else if(revision.observedKeyboard)pending.keyboard=static_cast<std::uint8_t>(
            (*revision.observedKeyboard<<5)|(*revision.observedKeyboard==0?2:0));
    }
    return superseded;
}
SettingsCompletionResolution resolveSettingsCompletion(SettingsSnapshot& pending,bool& clearActual,
    bool& success,std::wstring& message,const SettingsObservationRevision& revision,bool unhandledFailure,bool loggerTouched){
    SettingsCompletionResolution result;
    result.superseded=resolveSettingsObservationRevision(pending,clearActual,message,revision);
    if(unhandledFailure){
        pending.valid=false;clearActual=true;success=false;
        result.invalidatePendingLogger=true;result.invalidateCurrentLogger=true;result.markLoggerTouched=true;
        message=L"Ошибка SETs: состояние не подтверждено.";
    }
    success=settingsCompletionConfirmed(success,result.superseded);
    if(result.superseded&&loggerTouched)result.invalidatePendingLogger=true;
    return result;
}
void executeSettingsWorkSteps(const SettingsWorkSteps& steps){
    const auto started=steps.clock();
    steps.action();
    const auto appliedAt=steps.clock();
    steps.observe();
    steps.verify();
    steps.supplemental(started,appliedAt);
    steps.cycleCheck();
    if(steps.saveChoice)steps.persist();
}
std::wstring executeSettingsSuccess(SettingsSuccessContext& c){
    std::optional<SettingsResultContext> classification;bool cycleCommand{};
    // Build classification only after action, observation and supplemental
    // reads: the action may change the state consulted by these callbacks.
    auto steps=c.steps;
    steps.cycleCheck=[&]{classification=c.classification();cycleCommand=validateSettingsCycle(*classification,c.previous,c.pending);};
    executeSettingsWorkSteps(steps);
    // Result construction can still throw after an applied/persisted command.
    // Never publish it as confirmed if the failure path must report an error.
    auto result=classifySettingsResult(*classification,c.pending,cycleCommand,c.initialMessage(),c.toWide);
    c.pendingSuccess=true;c.lastReadError.clear();
    return result;
}
std::wstring executeSettingsWorker(SettingsWorkerContext& c){
    return executeSettingsOperation([&]() -> std::wstring {
        SettingsWorkSteps steps{
            []{return static_cast<std::uint64_t>(GetTickCount64());},
            [&]{c.action();},
            [&]{c.pending=c.quickBattery?c.previous:collectSettingsSnapshot(c.snapshot());},
            [&]{if(c.expectedPower&&!observedGmPowerMatches(c.pending,*c.expectedPower,c.profile()))
                throw std::runtime_error(c.amdPower?"Повторное чтение EC не подтвердило применённую тройку AMD SPL/SPPT/FPPT":"Повторное чтение EC не подтвердило применённую тройку Intel PL1/PL2/PL4");},
            {},[&]{c.persist(c.pending);},
            [&](std::uint64_t started,std::uint64_t appliedAt){collectSettingsSupplemental(c.pending,c.supplemental(started,appliedAt));},c.saveChoice};
        SettingsSuccessContext success{std::move(steps),c.previous,c.pending,c.classification,c.initialMessage,
            c.pendingSuccess,c.lastReadError,c.toWide};
        return executeSettingsSuccess(success);
    },c.failureContext,c.clearActual);
}
bool validateSettingsCycle(const SettingsResultContext& c,const SettingsSnapshot& previous,const SettingsSnapshot& actual){
    const bool cycleCommand=c.saveChoice&&c.hasCycle&&!c.hasHid&&
        (c.cycleTrigger||(c.colorChoice&&previous.cycle.active));
    if(cycleCommand&&!actual.cycle.active)throw std::runtime_error(actual.cycle.message.empty()?"Цикл 6 не запущен":actual.cycle.message);
    return cycleCommand;
}
std::wstring classifySettingsResult(const SettingsResultContext& c,const SettingsSnapshot& actual,bool cycleCommand,
        std::wstring result,std::wstring (*toWide)(const std::string&)){
    // Preserve existing override order; these are observed configuration,
    // never a claim that charge stopping or visible lighting was measured.
    if(c.hasEc&&!c.hasOem&&!c.hasGm&&!c.hasNativeBattery&&actual.valid&&c.saveChoice&&c.batteryUpper){
        const int upper=actual.battery&ec::kBatteryChargeLimitMask;
        if(upper==85||upper==95||upper==60||upper==10){
            const bool enabled=(actual.batteryGate&4)!=0;
            if(actual.batteryPlatform==7)result=L"Батарея: платформа 7, порог записан; физическое ограничение не проверено.";
            else if(c.saveChoice||!enabled)result=(upper==10||upper==60)?L"Порог записан; остановка зарядки не проверена.":enabled?L"Батарея: включение ограничения подтверждено EC.":L"Батарея: порог сохранён; включение ограничения не подтверждено EC.";
        }
    }
    if(c.hasNativeBattery&&((c.saveChoice&&c.batteryUpper)||(!c.saveChoice&&(!actual.nativeBattery.writable||actual.nativeBattery.configuredUpper))))
        result=toWide(actual.nativeBattery.status);
    if(c.nativeMode)result=L"Режим батареи подтверждён чтением EC.";
    if(c.saveChoice&&!c.hasOem&&c.batteryPolicy)result=L"Платформа и ROM ID подтверждены чтением EC.";
    if(cycleCommand)result=L"Цикл 6 запущен; смену цветов выполняет программа.";
    if(c.hasHid&&c.saveChoice&&c.lightingChoice)result=L"Команда HID принята; настройки зоны сохранены. Физический результат не измерен.";
    return result;
}
}

#include "SettingsPowerSelectionWindows.h"
#include "SettingsPersistenceWindows.h"
#include "SettingsActionWindows.h"
#include "BatteryChargeWindows.h"
#include "ApuPowerPolicyWindows.h"
#include "UniwillControlOwnerWindows.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/DeviceProfile.h"
#include "TextWindows.h"
#include <cmath>
#include <format>
#include "../../devices/lenovo/legion-kwcn/ControlWindows.h"
namespace mc::gui {
using mc::windows::encoding::wide;
SettingsOperationsWindows::Backends SettingsOperationsWindows::acquireBackends(bool preview,bool hasEc,const BackendFactories& factories){
    Backends result;
    if(!hasEc&&!preview)result.laptop=factories.laptop();
    if(!preview){result.gm=factories.gm();result.battery=factories.battery();result.voltage=factories.voltage();result.lighting=factories.lighting();}
    return result;
}
SettingsOperationsWindows::Backends SettingsOperationsWindows::acquireBackends(bool preview,bool hasEc){
    return acquireBackends(preview,hasEc,{[]{return createLaptopControl();},[]{return windows::gm6Controller();},
        []{return windows::nativeBatteryController();},[]{return windows::nativeBatteryVoltageController();},[]{return windows::iteLightingController();}});
}
bool SettingsOperationsWindows::ecVerified()const{return ec&&ec->boardVerified();}
bool SettingsOperationsWindows::fanNeedsRestore()const{return controller&&controller->status().needsRestore;}
bool SettingsOperationsWindows::honorBatteryBackend()const{return windows::honorBatteryBackend(oemControl.get());}
bool SettingsOperationsWindows::honorPowerBackend()const{return windows::honorCustomPowerBackend(oemControl.get());}
bool SettingsOperationsWindows::nativePowerBackend()const{return windows::nativeCustomPowerBackend(oemControl.get());}
SettingsOperationsWindows::SettingsOperationsWindows(View& view,EcAccess* access,std::function<void(std::string)> logger,std::shared_ptr<windows::FanController> control,BoardDescriptor board,bool viewing,ResourceWorker::ThreadFactory factory)
    :batteryCardView(initialBatteryCardView(deviceProfile(board).id)),intelMonitor(viewing?nullptr:std::make_unique<windows::IntelTelemetryReader>()),batteryReader(viewing?nullptr:std::make_unique<MonitoringReader>(access)),pageBoard(std::move(board)),preview(viewing),gm5Ui(gmAmdProfile(deviceProfile(pageBoard).id)),gm6NativePower(deviceProfile(pageBoard).id=="mechrevo-gm6ir0c"),
    intelUi(ramFanControlProfile(deviceProfile(pageBoard).id)),intelCpuUi((intelUi&&!gm5Ui)||(deviceProfile(pageBoard).id=="eluktronics-hydroc16g2"||deviceProfile(pageBoard).id=="lenovo-legion-kwcn")),huaweiUi(deviceProfile(pageBoard).id=="huawei-hlyl-wxx9"),ec(access),controller(std::move(control)),log(std::move(logger)),operations(std::move(factory)),view_(view){
        if(!preview&&!deviceProfile(pageBoard).id.empty())try{
            const auto selection=windows::loadCustomSelection(deviceProfile(pageBoard).id);selectedCustomPreset=selection.selected;savedCustomIntent=selection.active;
            if(customBaseSupported(deviceProfile(pageBoard).id))customBase=selectedCustomBase(deviceProfile(pageBoard).id);
        }catch(const std::exception& e){if(log)log(std::string("Custom presets: ")+e.what());}
        auto backends=acquireBackends(preview,bool(ec));
        oemControl=std::move(backends.laptop);gm6Controller=std::move(backends.gm);
        nativeBattery=std::move(backends.battery);voltageBattery=std::move(backends.voltage);iteLighting=std::move(backends.lighting);

        std::string cycleError;loadKeyboardCycleSettings(cycleSettings,cycleError);
        if(!preview&&!oemControl&&!iteLighting&&(gm6Controller||(ec&&ec->supports(DeviceFeature::Backlight))))
            cycle=std::make_unique<KeyboardColorCycle>([this]{return mc::gui::readLighting(lightingHardwareContext());},[this](const auto& c){return mc::gui::executeLightingResource(lightingHardwareContext(),LightingWriteRequest::singleZone(c,LightingWriteOrigin::Cycle)).keyboard.value();});
        if(!cycleError.empty()&&log)log(cycleError);

}
void SettingsOperationsWindows::initializePowerDraft(){
        if(gm6Controller){powerValues={0,0,0};chosenPowerMode=-1;std::string draftError;
            if(const auto saved=gm6Controller->loadPowerDraft(draftError))powerValues=*saved;
            if(!draftError.empty()&&log)log("GM6 Manual draft: "+draftError);
        }
}
void SettingsOperationsWindows::shutdown(){if(stopped_)return;stopped_=true;closing=true;operations.join();cycle.reset();if(!preview&&deviceProfile(pageBoard).id=="minisforum-hpbsd")windows::shutdownHpbsdPower();if(!preview&&deviceProfile(pageBoard).id=="lenovo-legion-kwcn")windows::shutdownLegionControl();}
std::string SettingsOperationsWindows::auditState(const State& s){return settingsSnapshotAudit(s);}
bool SettingsOperationsWindows::wujiePower() const{return deviceProfile(pageBoard).id=="wu-jie-15-xpro";}
int SettingsOperationsWindows::customPowerIndex() const{return gm6Controller||wujiePower()?5:4;}
windows::CustomModeContext SettingsOperationsWindows::customContext(){const bool honor=windows::honorCustomPowerBackend(oemControl.get());
        windows::CustomModeContext c{std::string(deviceProfile(pageBoard).id),gm6Controller,honor,wujiePower(),ec,controller,operation,&apuPowerReader,bool(oemControl)&&!honor&&!nativePowerBackend()};
        c.laptop=oemControl.get();c.log=log;return c;}
bool SettingsOperationsWindows::apuLockedByFanDefault(){return windows::customLockedByFanDefault(customContext());}
std::array<int,3> SettingsOperationsWindows::hlylMaximum(){return windows::honorCustomMaximum();}
int SettingsOperationsWindows::selectedPowerMode() const{
        return windows::selectedPowerMode({state,powerValues,chosenPowerMode,customPowerIndex(),savedCustomIntent,pendingPerformance.has_value(),gm6NativePower,bool(gm6Controller),bool(oemControl),bool(ec),powerEditing,wujiePower(),!oemControl||honorPowerBackend()||nativePowerBackend(),nativePowerBackend()});
    }
void SettingsOperationsWindows::applyPowerFromFields(){
        if(preview){previewState_.customPower(customPowerIndex());powerEditing=true;setStatus(L"Просмотр: изменения только в макете, на устройство не передаются.");view_.renderSelection();view_.updateButtons();return;}
        // Enter-to-apply reaches this method without the WM_COMMAND button path.
        requestedCommand=0;requestedNotice=view_.notice({windows::SettingsActionKind::CustomEditor});
        if(!powerEditing){powerBeforeEdit=powerValues;powerOptionsBeforeEdit=powerOptionValues;}
        const bool honorPower=windows::honorCustomPowerBackend(oemControl.get());
        const auto plan=windows::prepareCustomEditor({bool(gm6Controller),honorPower,powerEditing,state.gm6.writable,state.gm6.pl4Double,
            [this]{return gmPowerDoubleCodec(deviceProfile(pageBoard).id);},[]{return windows::apuOnBattery();},[this]{return apuLockedByFanDefault();},bool(oemControl)&&!honorPower&&!nativePowerBackend(),nativePowerBackend()},powerValues,
            {[this]()->std::optional<std::array<int,3>>{std::array<int,3> values{};if(!view_.readPowerFields(values))return {};return values;},
             [](auto& values){windows::loadCustomManualDraft(values);},windows::prepareHonorCustomDraft,windows::honorCustomManualAccepted,
             [this](auto){const auto library=loadCustomPresetLibrary(deviceProfile(pageBoard).id);const auto& preset=*library.slots[selectedCustomPreset];return preset.powerRecipe?oemControl->customCpuPowerRecipe(preset.powerRecipe-1):preset.power;}});
        const auto decision=plan.decision;
        using CustomDecision=windows::CustomClickDecision;
        if(decision==CustomDecision::Reject)return;
        if(plan.error==windows::CustomPrepareError::Fields)return;
        if(plan.error==windows::CustomPrepareError::HonorRange){setStatus(L"Honor: от сети 5–45/45/54 Вт, от батареи 5–30/30/35 Вт (Длительно/Средняя/Пик).");return;}
        if(decision==CustomDecision::OpenGm){powerEditing=true;view_.renderSelection();view_.updateButtons();setStatus(L"Отредактируйте и нажмите Custom");return;}
        windows::SettingsAction apply{windows::SettingsActionKind::CustomEditor,selectedCustomPreset};apply.customPlan=plan;
        if(powerEditing&&!view_.readPowerOptions(powerOptionValues))return;
        apply.powerOptions=powerOptionValues;
        if(decision==CustomDecision::ApplyGm){
            const auto values=plan.values;powerValues=values;pendingPerformance=customPowerIndex();
            start(apply,gm5Ui?L"Применяю лимиты AMD APU…":L"Применяю Intel PL1/PL2/PL4…",L"Лимиты подтверждены чтением EC.");return;
        }
        // Manual is a two-click editor, NOT a persistent edit mode inferred
        // from EC overrides. First click loads the saved draft without EC writes.
        // Second click applies/saves; success returns to the device observation.
        if (decision==CustomDecision::OpenNative){
            const auto library=loadCustomPresetLibrary(deviceProfile(pageBoard).id);const auto& preset=*library.slots[selectedCustomPreset];
            powerOptionValues=preset.powerRecipe?oemControl->customPowerOptionRecipe(preset.powerRecipe-1):preset.powerOptions;
            for(const auto& field:state.oem.customPowerOptions)if(!powerOptionValues.contains(field.key)&&field.actual)powerOptionValues[field.key]=*field.actual;
            powerValues=plan.values;powerEditorsInitialized=true;powerEditing=true;view_.renderSelection();view_.updateButtons();
            setStatus(L"Custom: отредактируйте параметры и нажмите Custom ещё раз для применения.");return;
        }
        if(decision==CustomDecision::ApplyNative){
            powerValues=plan.values;pendingPerformance=customPowerIndex();view_.renderSelection();
            start(apply,L"Применяю Custom через ядро…",L"Параметры подтверждены аппаратным чтением. Текущие вентиляторы сохранены.");return;
        }
        if (decision==CustomDecision::OpenHonor||decision==CustomDecision::OpenEc) {
            if(decision==CustomDecision::OpenHonor){
                powerValues=plan.values;powerEditing=true;view_.renderSelection();view_.updateButtons();
                setStatus(L"Honor Custom: отредактируйте лимиты и нажмите Custom ещё раз.");return;
            }
            powerValues=plan.values;
            // An explicit Wujie draft is initialized even before the first
            // accepted EC sample. A later poll must not overwrite user input.
            if(wujiePower())powerEditorsInitialized=true;
                powerEditing = true;
            view_.renderSelection(); view_.updateButtons();
            setStatus(L"Редактирование Custom. Нажмите Custom ещё раз, чтобы применить.");
            if (log) log("SETs APU Manual editor opened: saved draft shown; EC writes=0");
            return;
        }
        const auto values=plan.values;
        if(decision==CustomDecision::ApplyHonor){
            powerValues=values;pendingPerformance=4;view_.renderSelection();
            start(apply,L"Применяю лимиты Honor через SMU…",L"Лимиты подтверждены чтением SMU.");return;
        }
        powerValues = values;
        requestedCommand = 0;
        requestedNotice = view_.notice({windows::SettingsActionKind::CustomEditor});
        pendingPerformance = customPowerIndex(); view_.renderSelection();
        start(apply, L"Применяю ручные лимиты APU…", wujiePower()?L"Запрос записан. Контролы показывают итоговые лимиты EC; SMU — для сверки.":L"Лимиты записаны в EC. Контролы показывают фактические значения SMU.");
    }
bool SettingsOperationsWindows::batteryAutoActive() const{
        return windows::batteryPolicyActive(state,bool(nativeBattery));
    }
int SettingsOperationsWindows::batteryIndex() const{
        return windows::selectedBatteryThreshold(state,bool(nativeBattery),bool(oemControl),deviceProfile(pageBoard).id);
    }
int SettingsOperationsWindows::performanceIndexFromRaw(std::uint8_t raw){
        const auto value = raw & ec::kPerformancePresetMask;
        return value == 0x80 ? 0 : value == 0x10 ? 2 : value == 0 ? 1 : -1;
    }
int SettingsOperationsWindows::performanceIndex() const{ return performanceIndexFromRaw(state.performance); }
LightingConfiguration SettingsOperationsWindows::hidConfiguration()const{
        const auto info=state.lighting.find(selectedLightingZone);return info&&info->configured?*info->configured:LightingConfiguration{};
    }
void SettingsOperationsWindows::selectLightingZoneLocal(LightingZone zone){
        selectedLightingZone=zone;projectSettingsHidZone(state,zone);
    }
void SettingsOperationsWindows::applyVoltageMode(int mode){
        if(preview){previewState_.batteryMode=mode;previewState_.batteryUpper=-1;setStatus(L"Просмотр: изменения только в макете, на устройство не передаются.");view_.renderSelection();view_.updateButtons();return;}
        const bool lenovo=oemControl&&oemControl->chargePolicyId()=="lenovo-legion-kwcn-energy";
        if(preview||(!voltageBattery&&!lenovo)||(busy&&!reading))return;
        requestedCommand=0;
        // This path bypasses the generic WM_COMMAND dispatcher. Never inherit
        // an earlier lighting/power notification for a battery operation.
        requestedNotice=view_.notice({windows::SettingsActionKind::BatteryVoltage,mode});
        start(windows::SettingsAction{windows::SettingsActionKind::BatteryVoltage,mode},lenovo?L"Применяю штатный режим зарядки…":L"Применяю штатный профиль напряжения…");
    }
void SettingsOperationsWindows::applyBatteryAging(bool enabled){
    if(preview){previewState_.batteryAging=enabled?1:0;setStatus(L"Просмотр: изменения только в макете, на устройство не передаются.");view_.renderSelection();view_.updateButtons();return;}
    if(preview||!ec||(busy&&!reading)||!state.ecBattery.aging.writable)return;
    requestedCommand=0;
    windows::SettingsAction action{windows::SettingsActionKind::BatteryAging};action.enabled=enabled;
    requestedNotice={"battery","Подменить показания батареи","Применяю…",osd::Icon::Battery};
    start(action,L"Применяю подмену показаний батареи…");
}
SingleZoneModel SettingsOperationsWindows::lightingModel() const{return intelUi&&!iteLighting?SingleZoneModel::Gm6:SingleZoneModel::Wujie;}
const KeyboardPulsePalette& SettingsOperationsWindows::pulsePalette()const{return keyboardPulsePalette(lightingModel(),deviceProfile(pageBoard).id);}
std::filesystem::path SettingsOperationsWindows::lightingPreferences()const{return portableSettingsPath(gm6Controller?std::string(gmSettings(deviceProfile(pageBoard).id)):"device-settings.ini");}
LightingControlContext SettingsOperationsWindows::lightingContext()const{return {iteLighting.get(),gm6Controller.get(),oemControl.get(),ec,cycle.get(),selectedLightingZone,iteLighting?hidConfiguration():LightingConfiguration{},lightingModel(),std::string(deviceProfile(pageBoard).id),lightingPreferences(),cycleSettings,gm6NativePower};}
LightingControlContext SettingsOperationsWindows::lightingHardwareContext()const{LightingControlContext context;context.gm=gm6Controller.get();context.ec=ec;return context;}
int SettingsOperationsWindows::effectIndex() const{
        return observedLightingEffect(state,bool(iteLighting),selectedLightingZone);
    }
int SettingsOperationsWindows::brightnessMaximum() const{return deviceProfile(pageBoard).keyboardBrightnessMaximum;}
std::optional<int> SettingsOperationsWindows::currentBrightness() const{
        return observedLightingBrightness(state,preview,bool(iteLighting),bool(oemControl),selectedLightingZone);
    }
int SettingsOperationsWindows::brightnessIndex() const{
        return observedLightingBrightnessIndex(state);
    }
void SettingsOperationsWindows::setStatus(std::wstring text){
        if (text == statusText) return;
        statusText = std::move(text);
        view_.statusChanged(statusText);
    }
void SettingsOperationsWindows::start(std::optional<windows::SettingsAction> action, const wchar_t* message,
               const wchar_t* successMessage, bool passive,
               std::uint64_t existingToken, std::shared_ptr<DeviceOperation> reservation,bool keyboardIdleOnly){
        if(!batteryReader||!intelMonitor)return;
        audit::Scope dispatch("settings.dispatch","\"control_id\":"+std::to_string(requestedCommand),audit::capture(),!passive);
        auto admitted=operations.admit(preview,ec||oemControl||gm6Controller||nativeBattery||iteLighting||
            (action&&action->kind==windows::SettingsActionKind::BatteryView),passive,std::move(reservation));
        using Decision=SettingsDispatch::Decision;
        switch(admitted.decision){
        case Decision::Preview:pendingCustomBase.reset();dispatch.finish("rejected","\"reason\":\"model_preview\"");return;
        case Decision::NoBackend:pendingCustomBase.reset();dispatch.finish("rejected","\"reason\":\"no_supported_backend\"");return;
        case Decision::Busy:pendingCustomBase.reset();dispatch.finish("rejected","\"reason\":\"busy\"");return;
        case Decision::PassiveGateBusy:case Decision::PassiveAlreadyRunning:return;
        case Decision::GateBusy:pendingCustomBase.reset();setStatus(L"Ожидайте завершения текущего переключения EC.");dispatch.finish("rejected","\"reason\":\"operation_gate_busy\"");return;
        case Decision::Start:case Decision::Queue:break;
        }
        reservation=std::move(admitted.reservation);
        const int command = requestedCommand;
        const auto token = !passive ? (existingToken ? existingToken : view_.beginNotice(requestedNotice)) : 0;
        if (admitted.decision==Decision::Queue) {
            operations.enqueue({std::move(action), message ? message : L"", successMessage, requestedNotice, token, std::move(reservation), command});
            view_.updateButtons();
            dispatch.finish("queued");
            return;
        }
        busy = true;
        operation = std::move(reservation);
        reading = passive;
        idleObservation=keyboardIdleOnly;
        operationToken = token; activeNotice = requestedNotice;
        readRevision = revision;
        readNativeProfileRevision=nativeProfileRevision;
        pending = {};
        pendingBatteryView.reset();
        pendingLoggerObservation.reset();
        pendingLoggerTouched=false;
        pendingSuccess = false;pendingClearActual=false;
        expectedGmPower.reset();
        operationSuccess.reset();pendingPresetApplied=false;
        operations.begin(passive);
        if (message && *message) setStatus(message);
        if (!passive) windows::recordPowerAction("settings_operation", "requested", -1, message ? mc::windows::encoding::utf8(message) : "");
        if (!passive || !loaded) view_.updateButtons();
        const auto semantics=windows::settingsActionSemantics(action,gm6NativePower,passive);
        const bool quickProfile=semantics.quickProfile;
        const bool nativeModeCommand=semantics.nativeBatteryMode;
        const bool quickBattery=nativeModeCommand&&state.valid&&voltageBattery;
        try{operations.launch([this, action = std::move(action), success = std::wstring(successMessage), command, saveChoice = !passive,semantics,quickProfile,nativeModeCommand,quickBattery,keyboardIdleOnly,previous=state,auditId=operation?operation->actionId():audit::capture()] {
            SettingsExecutionContext work{previous,pending,command,action&&action->kind==windows::SettingsActionKind::BatteryVoltage?action->value:-1,auditId,
                {saveChoice,quickBattery,quickProfile,gm6NativePower,intelCpuUi,gm5Ui,nativeModeCommand,
                    semantics.batteryUpper,semantics.batteryPolicy,semantics.cycleTrigger,semantics.colorChoice,semantics.lightingChoice,keyboardIdleOnly},
                [this]{return SettingsSnapshotContext{ec,oemControl.get(),gm6Controller.get(),cycle.get(),iteLighting.get(),selectedLightingZone};},
                expectedGmPower,[this]{return deviceProfile(pageBoard).id;},[&]{if(action)runAction(*action);},
                [&](const State& actual){rememberChoice(actual,command,semantics);},nativeBattery.get(),voltageBattery.get(),
                *batteryReader,*intelMonitor,apuPowerReader,[this]{return operations.priorityPending();},log,lastApuLog,
                [&]{return operationSuccess.value_or(success);},reading,pendingSuccess,pendingClearActual,lastReadError,wide,mc::windows::encoding::utf8,
                [this](std::wstring result){pendingMessage=std::move(result);}};
            executeSettingsExecution(work);
        },[this]{if(ready_)ready_();});}
        catch(...){
            // No worker started; release its reservation and finish the same
            // user notification without applying or persisting a command.
            operations.finish();busy=false;reading=false;operation.reset();
            pendingSuccess=false;pendingLoggerTouched=false;pendingLoggerObservation.reset();
            pendingPerformance.reset();pendingBatteryView.reset();pendingCustomPreset.reset();pendingPresetValues.reset();pendingCustomBase.reset();pendingPresetApplied=false;
            const std::wstring message=L"Не удалось запустить обработчик SETs.";
            setStatus(message);view_.updateButtons();
            try{throw;}
            catch(const std::exception& error){try{if(log)log(std::string("SETs worker start failed: ")+error.what());}catch(...){}}
            catch(...){try{if(log)log("SETs worker start failed: unknown exception");}catch(...){}}
            if(operationToken){auto result=activeNotice;result.detail=mc::windows::encoding::utf8(message);result.error=true;
                view_.completeNotice(operationToken,std::move(result));operationToken=0;}
            dispatch.finish("failed","\"reason\":\"worker_start\"");return;
        }
        dispatch.finish("queued");
    }
void SettingsOperationsWindows::rememberChoice(const State& actual,int command,const windows::SettingsCommandSemantics& semantics){
        if(semantics.remember==SettingsRememberChoice::Ignore)return;
        rememberSettingsChoice({actual,semantics.remember,semantics.rememberIndex,command,bool(iteLighting),bool(oemControl),bool(gm6Controller),bool(nativeBattery),
            [this]{return lightingContext();},[this]{return cycle&&cycle->status().active;},
            [this]{return lightingPreferences();},[this]{return wujiePower();},log});
    }
void SettingsOperationsWindows::readState(bool timerTick){
        if(preview){setStatus(L"Просмотр профиля · аппаратное управление отключено");view_.updateButtons();return;}
        const bool systemBatteryOnly=!ec&&!oemControl&&!gm6Controller&&!nativeBattery;
        const auto now=GetTickCount64();const bool idleOnly=timerTick&&wujiePower()&&loaded&&now-lastFullPoll<3000;
        if(!idleOnly)lastFullPoll=now;
        start(std::nullopt, loaded ? L"" : systemBatteryOnly?L"Читаю данные батареи…":L"Читаю состояния EC…",
            loaded ? L"" : systemBatteryOnly?L"Батарея: системные показания. Управление ограничением зарядки для этой модели не подключено.":L"Состояния EC обновлены.", true,0,{},idleOnly);
    }
void SettingsOperationsWindows::complete(){
        if (!operations.takeCompletion()) return;
        if (operations.telemetrySkipped()) {
            busy=false;reading=false;operations.finish();operation.reset();
            if(auto queued=operations.takeQueued()){
                auto next=std::move(*queued);requestedNotice=std::move(next.notice);requestedCommand=next.command;
                start(std::move(next.action),next.message.c_str(),next.success.c_str(),false,next.token,std::move(next.reservation));
            }
            return;
        }
        std::wstring message=pendingMessage;
        const bool passive = reading;
        const bool idleOnly=idleObservation;
        const auto completion=resolveSettingsCompletion(pending,pendingClearActual,pendingSuccess,message,
            {readNativeProfileRevision,nativeProfileRevision,readRevision,revision,passive,state.valid,observedKeyboard},
            bool(operations.unhandledFailure()),pendingLoggerTouched);
        const bool superseded=completion.superseded;
        if(completion.invalidatePendingLogger)pendingLoggerObservation.reset();
        if(completion.invalidateCurrentLogger)loggerObservation.reset();
        if(completion.markLoggerTouched)pendingLoggerTouched=true;
        const auto display=transitionSettingsDisplay(state,pending,pendingClearActual,superseded,!iteLighting&&!oemControl,
            lightingModel(),loaded,ecDisplayCache,oemDisplayCache,keyboardDisplayCache);
        const bool changed=display.changed;
        if(display.accepted&&idleOnly)audit::state("keyboard.idle",audit::keyboardIdle(state.keyboardIdle));
        if (display.accepted&&!idleOnly) {
            auto shown=auditState(state);shown.pop_back();shown+=",\"selected_upper_index\":"+std::to_string(batteryIndex())+",\"selected_brightness\":"+audit::optional(currentBrightness())+",\"keyboard_retained\":"+(state.keyboardRetained?"true":"false")+",\"ec_retained\":"+(state.ecBattery.retainedForDisplay?"true":"false")+",\"ec_sampled_at_monotonic_ms\":"+std::to_string(state.ecBattery.sampledAtMs)+",\"health_stale\":"+(state.batteryTelemetry.healthStale?"true":"false")+",\"raw_7C5\":"+audit::optional(state.keyboardPulseRaw)+"}";
            if(wujiePower()){
                shown.pop_back();shown+=",\"wujie_power\":{\"selected_button\":"+std::to_string(selectedPowerMode())+
                    ",\"raw_751\":"+std::to_string(state.power.rawMode)+",\"overrides\":["+audit::optional(state.power.overrides.sustained)+","+
                    audit::optional(state.power.overrides.shortBoost)+","+audit::optional(state.power.overrides.peak)+"]}}";
            }
            audit::state("SETs.presentation",shown);
        }
        if(preview||!display.acceptedBattery)batteryResources.clear();
        else batteryResources.update(pageBoard,state.batteryTelemetry);
        if(pendingLoggerTouched){loggerObservation=pendingLoggerObservation;pendingLoggerObservation.reset();pendingLoggerTouched=false;
            view_.loggerChanged();}
        const auto observedLimits=observedSettingsPowerLimits(state,false,wujiePower());
        if(!powerEditing)for(const auto& field:state.oem.customPowerOptions)if(field.actual)powerOptionValues[field.key]=*field.actual;
        if (!powerEditorsInitialized && observedLimits[0] && observedLimits[1] && observedLimits[2]) {
            powerValues = {std::clamp(static_cast<int>(std::lround(*observedLimits[0])), 5, kApuManualMaximum),
                std::clamp(static_cast<int>(std::lround(*observedLimits[1])), 5, kApuManualMaximum),
                std::clamp(static_cast<int>(std::lround(*observedLimits[2])), 5, kApuManualMaximum)};
            powerEditorsInitialized = true;
        }
        observedKeyboard.reset();
        busy = false;
        reading = false;
        operations.finish();
        operation.reset();
        if(!passive&&pendingCustomBase){if(pendingSuccess&&!superseded)customBase=*pendingCustomBase;pendingCustomBase.reset();view_.renderSelection();}
        if (!message.empty()) setStatus(message);
        const bool pendingSelection=windows::completeCustomSelection(
            {chosenPowerMode,savedCustomIntent,powerEditing,selectedCustomPreset,powerValues,pendingPerformance,
                pendingCustomPreset,pendingPresetValues,pendingPresetApplied},passive,superseded,pendingSuccess,customPowerIndex());
        const bool batteryViewChanged=!passive&&pendingSuccess&&pendingBatteryView.has_value();
        if(batteryViewChanged)batteryCardView=static_cast<BatteryCardView>(*pendingBatteryView);
        pendingBatteryView.reset();
        if (!idleOnly&&(changed || pendingSelection || batteryViewChanged)) view_.renderSelection();
        view_.updateButtons();
        if (!passive) windows::recordPowerAction("settings_operation", pendingSuccess ? "confirmed" : "failed", -1, mc::windows::encoding::utf8(message));
        if (!passive && operationToken) {
            auto result = activeNotice; result.detail = superseded?"Состояние изменилось; обновляю показания.":mc::windows::encoding::utf8(message); result.error = !pendingSuccess;
            if (pendingSuccess && result.key == "battery" && result.percent) {
                const int index = batteryIndex();
                if (index < 0 || index >= batteryThresholdCount || batteryThresholdValues[index] != *result.percent) {
                    result.error = true; result.detail = "EC не подтвердил выбранный предел зарядки.";
                }
            }
            view_.completeNotice(operationToken,std::move(result)); operationToken = 0;
        }
        if (auto queued=operations.takeQueued()) {
            auto next = std::move(*queued);
            requestedNotice = std::move(next.notice);
            requestedCommand = next.command;
            start(std::move(next.action), next.message.c_str(), next.success.c_str(), false, next.token, std::move(next.reservation));
        }else if(nativeProfileRefreshPending){
            nativeProfileRefreshPending=false;readState();
        }else if(!idleOnly&&shouldRestoreLightingCycle(passive,cycleRestorePending,gm6NativePower,static_cast<bool>(cycle),
                 state.gm6.keyboardRestoreConfirmed,state.gm6.keyboardSuspended,state.gm6.keyboardRestorePending)){
            auto reservation=reserveLightingCycleRestore(cycleRestorePending);if(!reservation)return;
            try{
                if(loadLightingCycleRestore(lightingPreferences(),cycleSettings)){
                    const auto settings=cycleSettings;requestedCommand=0;
                    requestedNotice={"keyboard","Цикл 6","Восстанавливаю сохранённый эффект…",osd::Icon::Keyboard};
                    start(windows::SettingsAction::cycleRequest(settings,windows::LightingCycleIntent::Effect),L"Восстанавливаю сохранённый цикл подсветки…",
                        L"Сохранённый цикл восстановлен.",false,0,std::move(reservation));
                }
            }catch(const std::exception& error){if(log)log("CONFIG Cycle6 restore failed: "+std::string(error.what()));}
        }
    }
void SettingsOperationsWindows::observeKeyboardLevel(int level){
        ExternalLightingLevelContext context{state,revision,observedKeyboard,preview,bool(oemControl),busy,reading,
            []{return deviceOperations().busy();},[this](int value){rememberObservedLightingLevel(lightingContext(),value);},log};
        if(observeExternalLightingLevel(context,level))view_.renderSelection();
    }
osd::Message SettingsOperationsWindows::lightingNotice(std::string action,std::optional<int> level)const{
        return osd::lighting(iteLighting?selectedLightingZone:LightingZone::Keyboard,std::move(action),level,brightnessMaximum());
    }
windows::BatterySettingsContext SettingsOperationsWindows::batteryContext()const{return {nativeBattery.get(),voltageBattery.get(),oemControl.get(),ec,std::string(deviceProfile(pageBoard).id),log};}
void SettingsOperationsWindows::runAction(const windows::SettingsAction& action){
        const windows::SettingsActionServices services{
            [this]{return batteryContext();},
            [this]{return lightingContext();},
            [this]{return customContext();},&pendingLoggerTouched,&pendingLoggerObservation,log};
        const auto result=windows::executeSettingsAction(action,services);
        if(result.batteryView)pendingBatteryView=result.batteryView;
        if(result.gmExpected)expectedGmPower=*result.gmExpected;
        if(result.customValues){if(result.confirmGm)expectedGmPower=*result.customValues;
            pendingPresetValues=*result.customValues;pendingPresetApplied=true;operationSuccess=L"Пресет Custom применён.";}
    }
void SettingsOperationsWindows::selectCustomPreset(int slot){
        if(preview){previewState_.customPreset(slot,customPowerIndex());powerEditing=previewState_.customEditing;setStatus(L"Просмотр: изменения только в макете, на устройство не передаются.");view_.renderSelection();view_.updateButtons();return;}
        if(slot<0||slot>=3||(!gm6Controller&&!oemControl&&(!ec||!controller)))return;
        if(oemControl&&!honorPowerBackend()&&!nativePowerBackend())return;
        requestedCommand=0;pendingCustomPreset=slot;
        requestedNotice={"performance","Custom "+std::to_string(slot+1),"Применяю пресет Custom…",osd::Icon::Power};
        start(windows::SettingsAction{windows::SettingsActionKind::CustomPreset,slot},L"Применяю пресет Custom…");
    }
bool SettingsOperationsWindows::customBaseAvailable()const{
    return !preview&&loaded&&state.valid&&customBaseSupported(deviceProfile(pageBoard).id)&&selectedPowerMode()==customPowerIndex()&&
        (!busy||(reading&&!operations.hasQueued()))&&!deviceOperations().interactiveBusy();
}
int SettingsOperationsWindows::selectedCustomBaseValue()const{
    if(!state.valid||selectedPowerMode()!=customPowerIndex())return -1;
    if(gm6Controller&&state.gm6.connected)return state.gm6.mode==1?0:state.gm6.mode==2?1:-1;
    if(wujiePower()){const auto raw=state.power.rawMode&0xB0;return raw==0?0:raw==0x10?1:-1;}
    return -1;
}
void SettingsOperationsWindows::setCustomBase(int base){
    if((base!=0&&base!=1)||!customBaseAvailable())return;
    pendingCustomBase=base;requestedCommand=0;
    requestedNotice={"performance","Custom Base","Применяю базу Custom…",osd::Icon::Power};
    start(windows::SettingsAction{windows::SettingsActionKind::CustomBase,base},L"Применяю базу Custom…",L"База Custom подтверждена EC.");
}
void SettingsOperationsWindows::initializePowerEditors(){
        const auto startup=windows::loadPowerStartup(deviceProfile(pageBoard).id,!preview&&!gm6Controller,
            windows::honorCustomPowerBackend(oemControl.get()),wujiePower(),{powerValues,chosenPowerMode,powerEditorsInitialized,savedCustomIntent});
        powerValues=startup.values;chosenPowerMode=startup.chosenMode;
        powerEditorsInitialized=startup.editorsInitialized;savedCustomIntent=startup.savedCustomIntent;

}
void SettingsOperationsWindows::selectBatteryView(BatteryCardView selected){
        if(preview){batteryCardView=selected;view_.renderSelection();return;}
        if(closing||(busy&&!reading))return;
        requestedCommand=0;requestedNotice={"battery","Режим ограничения зарядки","Применяю…",osd::Icon::Battery};
        start(windows::SettingsAction{windows::SettingsActionKind::BatteryView,static_cast<int>(selected)},L"Переключаю режим ограничения зарядки…");
}
void SettingsOperationsWindows::requestLogger(const std::string& address,const std::string& value,bool writing){
        if(preview||closing||(busy&&!reading))return;
        const auto prepared=prepareEcLogger(address,value,writing,loggerObservation);
        if(prepared.error==EcLoggerInputError::InvalidHex){setStatus(L"Неверный HEX адрес или байт");return;}
        if(prepared.error==EcLoggerInputError::ObservationRequired){setStatus(L"Сначала перечитайте выбранный адрес клавишей Enter");return;}
        requestedCommand=0;requestedNotice={"ec_logger","Логгер",writing?"Записываю байт EC…":"Читаю байт EC…",osd::Icon::Information};
        start(windows::SettingsAction::loggerRequest(*prepared.request),writing?L"Записываю байт EC…":L"Читаю байт EC…",writing?L"Байт записан; чтение обратно совпало.":L"Байт EC прочитан.");
    }
void SettingsOperationsWindows::changeCycleInterval(int seconds){
        if(preview||busy)return;
        if(seconds<1||seconds>3600){setStatus(L"Интервал цикла:1–3600сек");return;}
        std::string error;const auto edit=editLightingCycleInterval(cycleSettings,seconds,error,saveKeyboardCycleSettings);
        if(edit==LightingCycleEditResult::Failed){setStatus(wide(error));return;}
        if(edit!=LightingCycleEditResult::Saved)return;
        if(cycle&&cycle->status().active){const auto settings=cycleSettings;requestedCommand=0;requestedNotice={"keyboard","Цикл 6","Применяю интервал…",osd::Icon::Keyboard};start(windows::SettingsAction::cycleRequest(settings),L"Применяю интервал цикла…");}
    }
void SettingsOperationsWindows::changeCycleColor(int index,KeyboardColor color){
    if(preview||busy||!cycle||index<0||index>=6)return;
        std::string error;const auto edit=editLightingCycleColor(cycleSettings,index,color,error,saveKeyboardCycleSettings);
        if(edit==LightingCycleEditResult::Failed){setStatus(wide(error));return;}
        if(edit!=LightingCycleEditResult::Saved)return;
        requestedCommand=0;
        requestedNotice=lightingNotice(std::format("Цикл 6 · Цвет {} · #{:02X}{:02X}{:02X}",index+1,
            color[0],color[1],color[2]));
        const auto settings=cycleSettings;start(windows::SettingsAction::cycleRequest(settings,windows::LightingCycleIntent::Color),L"Применяю цвета цикла…");
}

TraySettingsState SettingsOperationsWindows::trayState()const{
    if(preview)return {};
    if(nativeBattery)return {}; // Tray percentage commands do not represent native named policies.
    TraySettingsState result;
    result.ready = state.valid && (!busy || (reading && !operations.hasQueued())) && ec && ec->boardVerified() && !deviceOperations().interactiveBusy();
    result.turboAvailable = true; // Native WUJIE firmware owns its AC/DC budget.
    result.batteryAuto = batteryAutoActive();
    result.apuPolicyReady = result.ready;
    result.performanceReady = result.ready && controller && !controller->status().needsRestore;
    result.keyboardSupported = state.valid && state.keyboardSupported && state.keyboardWritable;
    if (state.valid) {
        result.battery = batteryIndex(); result.performance = selectedPowerMode() == customPowerIndex() ? -1 : selectedPowerMode(); result.brightness = brightnessIndex();
    }
    return result;
}
void SettingsOperationsWindows::refreshForTray(bool invalidateObservation){

    if(invalidateObservation){++revision;++nativeProfileRevision;nativeProfileRefreshPending=busy;
        batteryResources.clear();
        loggerObservation.reset();
        view_.loggerChanged();
        view_.updateButtons();}
    if (!busy && (!ec||!ec->observationsPaused())) readState();

}
bool SettingsOperationsWindows::observePowerSupply(){auto observation=telemetryGate().tryObservation();if(!observation)return false;const bool onBattery=windows::apuOnBattery();if(state.onBattery==onBattery)return false;state.onBattery=onBattery;return true;}
bool SettingsOperationsWindows::cancelCustomEditor(){if(!powerEditing||busy)return false;
        if(preview){previewState_.customEditing=false;previewState_.power=-1;powerEditing=false;view_.renderSelection();view_.updateButtons();setStatus(L"Просмотр: изменения только в макете, на устройство не передаются.");return true;}
        powerValues=powerBeforeEdit;powerOptionValues=powerOptionsBeforeEdit;powerEditing=false;pendingPerformance.reset();
        view_.renderSelection();view_.updateButtons();setStatus(L"Редактирование Custom отменено. Лимиты не применялись.");
        audit::event("custom_edit_cancelled","\"ec_writes\":0");return true;}

void SettingsOperationsWindows::editPowerDraftField(int index,const std::wstring& text){
    if(preview){if(index>=0&&index<3)if(const auto value=windows::clampCustomDraftField(text.c_str(),0,100000))powerValues.at(index)=*value;return;}
    const auto context=customContext();const auto maximum=windows::customFieldMaximum(context,index);
    if(const auto value=windows::clampCustomDraftField(text.c_str(),windows::customFieldMinimum(context,index),maximum))powerValues.at(index)=*value;
}
void SettingsOperationsWindows::editPowerOptionDraft(std::string_view key,int value){
    if(!powerEditing)return;
    const auto found=std::find_if(state.oem.customPowerOptions.begin(),state.oem.customPowerOptions.end(),[&](const auto& field){return field.key==key;});
    if(found!=state.oem.customPowerOptions.end()&&found->accepts(value))powerOptionValues[found->key]=value;
}

}
