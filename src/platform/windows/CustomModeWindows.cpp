#include "CustomModeWindows.h"
#include "ApuPowerWindows.h"
#include "ApuPowerPolicyWindows.h"
#include "FanControlWindows.h"
#include "FanDeviceActionsWindows.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "MagicControl/CustomPresets.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/SettingsResourceControl.h"
#include <algorithm>
#include <cwchar>
#include <format>
#include <stdexcept>
namespace mc::windows {
namespace {
std::array<int,3> applyApuAllowedLimits(EcAccess& access,std::array<int,3> values){
    values=apuAllowedLimits(values); // Recheck live supply at apply/restore.
    applyManualPowerLimits(access,values); // Only 783/784/785, never FAN/battery flags.
    return values;
}
void applyApuAutomaticLimits(EcAccess& access){
    const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("CPU policy is owned by OEM: "+owner);
    resetManualPowerLimits(access);
}
PerformanceChange applyApuStockProfile(EcAccess& access,int profile,
        std::shared_ptr<FanController> fans,std::shared_ptr<DeviceOperation> operation){
    const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("CPU profile is owned by OEM: "+owner);
    if(fans)return fans->performance(profile,std::move(operation)).get();
    return selectStandalonePerformance(access,profile);
}
using PowerRoutes=ResourceControlTable<PowerWriteRequest,CustomModeContext,PowerWriteResult>;
ResourceControlKey powerKey(PowerWrite kind){
    switch(kind){
    case PowerWrite::Custom:return mc::settingsResourceControlKey(SettingsResourceOperation::PowerCustomWrite);
    case PowerWrite::CustomBase:return mc::settingsResourceControlKey(SettingsResourceOperation::PowerCustomBase);
    case PowerWrite::Preset:return mc::settingsResourceControlKey(SettingsResourceOperation::PowerCustomSelect);
    case PowerWrite::Automatic:return mc::settingsResourceControlKey(SettingsResourceOperation::PowerAutomatic);
    case PowerWrite::Economy:return mc::settingsResourceControlKey(SettingsResourceOperation::PowerEconomy);
    case PowerWrite::Allowed:return mc::settingsResourceControlKey(SettingsResourceOperation::PowerAllowed);
    case PowerWrite::Stock:return mc::settingsResourceControlKey(SettingsResourceOperation::PowerStock);
    case PowerWrite::CustomEditor:return mc::settingsResourceControlKey(SettingsResourceOperation::PowerCustomEditor);
    }throw std::invalid_argument("Unknown power resource command");
}
PowerWriteResult writeCustomPower(const PowerWriteRequest& r,const CustomModeContext& c){
    if(c.gm6){c.gm6->setPower(r.values);return {r.values,{}};}
    if(c.honor){if(!c.apu)throw std::runtime_error("Custom: missing Honor power reader");return {r.values,c.apu->applyHlylLimits(r.values)};}
    if(nativeCustomPowerBackend(c.laptop))return {c.laptop->setCustomPower(r.values,r.options),{}};
    if(!c.ec)throw std::runtime_error("Custom: device control unavailable");
    // Explicit Wujie Custom5..255 remains supply-independent. Its selected CPU base
    // and own limits are one coordinated command; no factory DC clipping here.
    const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("Custom power is owned by OEM: "+owner);
    if(c.fans)c.fans->customPower(r.values,c.operation).get();
    else static_cast<void>(selectStandaloneCustomPower(*c.ec,r.values));
    return {r.values,{}};
}
PowerWriteResult writeCustomBase(const PowerWriteRequest& r,const CustomModeContext& c){
    if(!customBaseSupported(c.profileId)||(r.slot!=0&&r.slot!=1))throw std::runtime_error("Custom Base: неподтверждённая модель или значение");
    const auto apply=[&](int base){
        if(c.gm6)c.gm6->setCustomBase(base);
        else if(c.wujie&&c.ec){
            const auto owner=uniwillControlOwner();if(!owner.empty())throw std::runtime_error("Custom Base is owned by OEM: "+owner);
            if(c.fans)c.fans->customBase(base,c.operation).get();
            else static_cast<void>(selectStandaloneCustomBase(*c.ec,base));
        }else throw std::runtime_error("Custom Base: backend unavailable");
    };
    apply(r.slot);
    try{rememberCustomBase(c.profileId,r.slot);}catch(const std::exception& e){
        throw std::runtime_error(std::string("Custom Base применён, но настройка не сохранена: ")+e.what());
    }
    return {};
}
PowerWriteResult writePowerPreset(const PowerWriteRequest&,const CustomModeContext&);
PowerWriteResult writeCustomEditor(const PowerWriteRequest&,const CustomModeContext&);
PowerWriteResult writeAutomaticPower(const PowerWriteRequest&,const CustomModeContext& c){
    if(c.gm6){c.gm6->resetCpuPower();return {};}
    if(!c.ec)throw std::runtime_error("Automatic power backend unavailable");
    applyApuAutomaticLimits(*c.ec);return {};
}
PowerWriteResult writeEconomyPower(const PowerWriteRequest&,const CustomModeContext& c){
    if(c.gm6){c.gm6->setEconomyPower();return {};}
    if(c.fanDefaultLocked?c.fanDefaultLocked():customLockedByFanDefault(c))
        throw std::runtime_error("В Default FANs ручное управление APU недоступно.");
    const bool battery=c.onBattery?c.onBattery():apuOnBattery();
    if(!c.ec)throw std::runtime_error("APU power limits are unavailable for this device");
    const auto values=applyApuAllowedLimits(*c.ec,apuPresetLimits(0,battery));
    if(c.log)c.log(std::format("SETs APU preset=0: Fast={} Slow={} STAPM={} W; EC 783/784/785 verified; fan writes=0; SMU confirmation follows in telemetry",values[2],values[1],values[0]));
    return {values,{}};
}
PowerWriteResult writeStockPower(const PowerWriteRequest& r,const CustomModeContext& c){
    if(r.slot<0||r.slot>2)throw std::invalid_argument("Unknown native CPU profile");
    if(c.gm6)return {c.gm6->setCpuPreset(r.slot),{}};
    if(c.laptop){c.laptop->setStockCpuProfile(r.slot);return {};}
    if(!c.ec)throw std::runtime_error("Native CPU profile is unavailable for this device");
    const auto change=applyApuStockProfile(*c.ec,r.slot,c.fans,c.operation);
    if(c.log)c.log(std::format("SETs native CPU profile={} mode751 before=0x{:02X} after=0x{:02X}; overrides cleared; FAN flags/tables retained; actual SMU limits follow in telemetry",r.slot,change.before,change.after));
    return {};
}
PowerWriteResult writeAllowedPower(const PowerWriteRequest& r,const CustomModeContext& c){
    if(!c.ec)throw std::runtime_error("Allowed power backend unavailable");
    return {applyApuAllowedLimits(*c.ec,r.values),{}};
}
const PowerRoutes& powerRoutes(){
    static const auto routes=[] {
        std::vector<PowerRoutes::Binding> entries;
        const auto add=[&](PowerWrite kind,PowerRoutes::Handler handler){const auto key=powerKey(kind);entries.push_back({key.kind,key.instance,key.operation,handler});};
        add(PowerWrite::Custom,writeCustomPower);add(PowerWrite::Preset,writePowerPreset);add(PowerWrite::Automatic,writeAutomaticPower);
        add(PowerWrite::CustomBase,writeCustomBase);
        add(PowerWrite::Economy,writeEconomyPower);add(PowerWrite::Allowed,writeAllowedPower);
        add(PowerWrite::Stock,writeStockPower);
        add(PowerWrite::CustomEditor,writeCustomEditor);
        return PowerRoutes(entries);
    }();return routes;
}
}
PowerWriteResult executePowerResource(const CustomModeContext& context,const PowerWriteRequest& request){
    if(nativeCustomPowerBackend(context.laptop)&&!((context.laptop->customPowerPolicyId()=="minisforum-hpbsd-windows-smu"&&context.profileId=="minisforum-hpbsd")||
       (context.laptop->customPowerPolicyId()=="lenovo-legion-kwcn-wmi"&&context.profileId=="lenovo-legion-kwcn")))throw std::runtime_error("Custom: native power profile not confirmed");
    if(context.unsupportedTyped&&request.kind!=PowerWrite::Stock)throw std::runtime_error("Custom: политика мощности для этого OEM backend не подключена");
    if(context.honor&&context.profileId!="huawei-hlyl-wxx9")throw std::runtime_error("Custom: профиль Honor не подтверждён");
    if((request.kind==PowerWrite::Preset||request.kind==PowerWrite::CustomEditor)&&(request.slot<0||request.slot>=3))
        throw std::invalid_argument("Custom preset: invalid slot");
    const auto result=powerRoutes().execute(powerKey(request.kind),request,context);
    // GM owners already persist this marker in their checked native methods.
    // Repeating it here used to duplicate the GUI write and its file reads.
    if(!context.gm6&&request.origin==PowerWriteOrigin::Explicit&&!context.profileId.empty()&&
        (request.kind==PowerWrite::Stock||request.kind==PowerWrite::Economy||request.kind==PowerWrite::Automatic))
        context.persistStockChoice(context.profileId,false); // Only after the checked writer returns.
    return result;
}
bool honorCustomPowerBackend(const LaptopControl* control) noexcept{
    return control&&control->customPowerPolicyId()=="huawei-hlyl-windows-smu";
}
bool nativeCustomPowerBackend(const LaptopControl* control) noexcept{
    return control&&(control->customPowerPolicyId()=="minisforum-hpbsd-windows-smu"||control->customPowerPolicyId()=="lenovo-legion-kwcn-wmi");
}
bool completeCustomSelection(CustomCompletionState c,bool passive,bool superseded,bool success,int customIndex) noexcept{
    const bool pendingSelection=!passive&&(c.pendingMode.has_value()||c.pendingPresetApplied);
    if(!passive){
        if(!superseded&&success&&c.pendingMode){c.chosenMode=*c.pendingMode;c.savedIntent=c.chosenMode==customIndex;c.editing=false;}
        if(success&&c.pendingPreset)c.selectedPreset=*c.pendingPreset;
        if(success&&c.pendingValues)c.values=*c.pendingValues;
        c.pendingValues.reset();c.pendingPreset.reset();c.pendingMode.reset();
        if(!superseded&&success&&c.pendingPresetApplied){c.editing=false;c.chosenMode=customIndex;c.savedIntent=true;}
    }
    return pendingSelection;
}
CustomPresetSelection initializeCustomSelection(const std::function<void()>& ensure,
    const std::function<CustomPresetLibrary()>& load){
    ensure();const auto library=load();return {library.selected,library.active};
}
CustomPresetSelection loadCustomSelection(std::string_view profile){
    return initializeCustomSelection([&]{ensureCustomPresets(profile);},[&]{return loadCustomPresetLibrary(profile);});
}
CustomClickDecision decideCustomClick(const CustomClickContext& c){
    if(c.unsupportedTyped)return CustomClickDecision::Reject;
    if(c.nativeCustom)return c.editing?CustomClickDecision::ApplyNative:CustomClickDecision::OpenNative;
    if(c.gm){
        if(!c.writable||(c.pl4Double&&!c.doubleCodecSupported())||c.onBattery())return CustomClickDecision::Reject;
        return c.editing?CustomClickDecision::ApplyGm:CustomClickDecision::OpenGm;
    }
    if(c.fanDefaultLocked())return CustomClickDecision::Reject;
    if(c.honor)return c.editing?CustomClickDecision::ApplyHonor:CustomClickDecision::OpenHonor;
    return c.editing?CustomClickDecision::ApplyEc:CustomClickDecision::OpenEc;
}
CustomEditorPlan prepareCustomEditor(const CustomClickContext& context,std::array<int,3> values,const CustomEditorActions& actions){
    const auto decision=decideCustomClick(context);
    if(decision==CustomClickDecision::OpenHonor)values=actions.prepareHonor(values);
    else if(decision==CustomClickDecision::OpenNative&&actions.nativeDraft)values=actions.nativeDraft(values);
    else if(decision==CustomClickDecision::OpenEc)actions.loadDraft(values);
    else if(decision==CustomClickDecision::ApplyGm||decision==CustomClickDecision::ApplyHonor||decision==CustomClickDecision::ApplyEc||decision==CustomClickDecision::ApplyNative){
        const auto fields=actions.readFields();
        if(!fields)return {decision,CustomPrepareError::Fields,values};
        values=*fields;
        if(decision==CustomClickDecision::ApplyHonor&&!actions.acceptHonor(values))
            return {decision,CustomPrepareError::HonorRange,values};
    }
    return {decision,CustomPrepareError::None,values};
}
void executeCustomEditorApply(const CustomEditorPlan& plan,const CustomApplyActions& actions){
    if(plan.error!=CustomPrepareError::None||(plan.decision!=CustomClickDecision::ApplyGm&&
        plan.decision!=CustomClickDecision::ApplyHonor&&plan.decision!=CustomClickDecision::ApplyEc&&plan.decision!=CustomClickDecision::ApplyNative))
        throw std::invalid_argument("Custom: request is not ready to apply");
    const auto applied=actions.apply(plan.values);
    if(plan.decision==CustomClickDecision::ApplyGm)actions.confirmGm(plan.values);
    if(plan.decision==CustomClickDecision::ApplyNative&&actions.log)actions.log("Custom native CPU limits confirmed by backend; FAN policy retained");
    if(plan.decision==CustomClickDecision::ApplyEc&&actions.log)
        actions.log(std::format("SETs APU Manual: Fast={} Slow={} STAPM={} W; EC 783/784/785 verified; fan writes=0; SMU confirmation follows in telemetry",
            applied[2],applied[1],applied[0]));
}
std::array<int,3> prepareHonorCustomDraft(std::array<int,3> values){return honorCustomAllowed(values);}
bool honorCustomManualAccepted(const std::array<int,3>& values){return values==honorCustomAllowed(values);}
std::array<int,3> honorCustomMaximum(){return apuOnBattery()?std::array<int,3>{30,30,35}:std::array<int,3>{45,45,54};}
std::array<int,3> honorCustomAllowed(std::array<int,3> values){const auto maximum=honorCustomMaximum();for(int i=0;i<3;++i)values[i]=std::clamp(values[i],5,maximum[i]);return values;}
std::array<int,3> honorCustomRecipe(int preset){
    // App presets, not claims about Huawei factory modes. No fan writes.
    constexpr std::array<std::array<int,3>,4> values{{{10,10,15},{25,25,30},{40,40,45},{45,45,54}}};
    if(preset<0||preset>3)throw std::invalid_argument("Honor: неизвестный пресет APU");
    return honorCustomAllowed(values[preset]);
}
int gmCustomFieldMaximum(std::string_view profile,int index){
    if(index<0||index>=3)throw std::out_of_range("Custom: invalid limit index");
    if(hydrocProfile(profile))return index==2?510:255;
    if(gmAmdProfile(profile))return gmAmdPowerMaximum(profile,index);
    return std::array{140,165,250}[index];
}
int customFieldMaximum(const CustomModeContext& c,int index){
    if(index<0||index>=3)throw std::out_of_range("Custom: invalid limit index");
    if(c.gm6)return gmCustomFieldMaximum(c.profileId,index);
    if(nativeCustomPowerBackend(c.laptop))return c.laptop->customCpuPowerMaximum()[index];
    return c.honor?honorCustomMaximum()[index]:kApuManualMaximum;
}
int customFieldMinimum(const CustomModeContext& c,int index){
    if(index>=0&&index<3&&nativeCustomPowerBackend(c.laptop))return c.laptop->customCpuPowerMinimum()[index];
    return c.gm6?0:5;
}
const wchar_t* customFieldsError(std::string_view profile,bool gm,bool honor) noexcept{
    if(profile=="lenovo-legion-kwcn")return L"Custom: значение вне диапазона WMI этого устройства. Проверьте пределы CPU, GPU, Tau и температур.";
    if(profile=="minisforum-hpbsd")return L"STAPM / Slow PPT / Fast PPT: запрос 5–255 Вт; STAPM ≤ Slow ≤ Fast. Итог подтверждается SMU.";
    if(gm){
        if(l16Profile(profile))return L"L16: SPL/SPPT 5–145 Вт, FPPT 5–174 Вт; 0/0/0 — штатные лимиты.";
        if(gmAmdProfile(profile))return L"AMD: SPL/SPPT 5–100 Вт, FPPT 5–200 Вт; 0/0/0 — штатные лимиты.";
        return L"Intel: PL1 10–140, PL2 10–165, PL4 10–250 Вт; 0/0/0 — штатные лимиты.";
    }
    return honor?L"Honor: Длительно/Средняя/Пик 5–45/45/54 Вт от сети, 5–30/30/35 Вт от батареи.":
        L"Введите три лимита мощности от 5 до 255 Вт.";
}
std::optional<std::array<int,3>> parseCustomFields(const std::function<std::wstring(int)>& read,
    const std::function<int(int)>& maximum,const std::function<int()>& minimum){
    std::array<int,3> values{};
    for(int i=0;i<3;++i){const auto text=read(i);const int max=maximum(i);const int min=minimum();
        const auto value=parseCustomField(text.c_str(),min,max);if(!value)return {};values[i]=*value;}
    return values;
}
std::optional<int> parseCustomField(const wchar_t* text,int minimum,int maximum){
    if(!text||!text[0])return {};
    wchar_t* end{};const long value=std::wcstol(text,&end,10);
    if(!end||*end||value<minimum||value>maximum)return {};
    return static_cast<int>(value);
}
std::optional<int> clampCustomDraftField(const wchar_t* text,int minimum,int maximum){
    if(!text||!text[0])return {};
    wchar_t* end{};const long value=std::wcstol(text,&end,10);
    if(!end||*end)return {};
    return static_cast<int>(std::clamp(value,static_cast<long>(minimum),static_cast<long>(maximum)));
}
bool customLockedByFanDefault(const CustomModeContext& c){
    if(c.gm6||c.honor||nativeCustomPowerBackend(c.laptop)||!c.fans)return false;
    const auto fan=c.fans->status();
    return fan.observedDefault==true&&!fan.busy&&!fan.applying&&!fan.error&&!fan.needsRestore&&!fan.suspended&&!fan.ecManual&&!fan.maximum&&!fan.controlled[0]&&!fan.controlled[1];
}
void loadCustomManualDraft(std::array<int,3>& values){
    DevicePreferences saved;std::string error;
    if(loadDevicePreferences(portableSettingsPath("device-settings.ini"),saved,error)&&saved[DeviceSetting::ManualStapm]&&saved[DeviceSetting::ManualSlow]&&saved[DeviceSetting::ManualFast])
        values={*saved[DeviceSetting::ManualStapm],*saved[DeviceSetting::ManualSlow],*saved[DeviceSetting::ManualFast]};
}
namespace {
void saveCustomDraft(const CustomModeContext& c,int slot,const std::array<int,3>& values,const PowerOptions& options){
    if(c.gm6){c.gm6->saveCustomPresetDraft(slot,values);return;}
    auto lib=loadCustomPresetLibrary(c.profileId);auto p=*lib.slots[slot];p.power=values;p.powerRecipe=0;p.powerOptions=options;
    if(nativeCustomPowerBackend(c.laptop)){
        const auto fans=c.nativeFans?c.nativeFans:nativeFixedFanController();if(!fans)throw std::runtime_error("Custom: native FAN owner unavailable");
        const auto state=fans->read();if(!state.curves)throw std::runtime_error("Custom: native FAN draft unavailable");
        for(const auto* device:deviceProfiles())if(device->id==c.profileId)for(int ch=0;ch<2;++ch)for(int i=0;i<device->fanPointCounts[ch];++i){const auto& v=state.curves->draft[ch][i];p.curves[ch][i]={v.temperature,i?state.curves->draft[ch][i-1].temperature:0,v.duty};}
        p.curvesReady=true;
    }
    if(c.fans){const auto fans=c.fans->status();
        for(int ch=0;ch<2;++ch)for(int i=0;i<16;++i){const auto& table=fans.applied[fans.ecManual?0:ch];const int next=i<15?table[i+1].temperature:255;
            p.curves[ch][i]={table[i].temperature,next==255?255:std::max(0,next-3),table[i].duty};}}
    lib.slots[slot]=p;lib.selected=slot;lib.active=true;saveCustomPresetLibrary(c.profileId,lib);
}
PowerWriteResult writeCustomEditor(const PowerWriteRequest& r,const CustomModeContext& c){
    const auto& values=r.values;const int slot=r.slot;
    if(c.gm6||c.honor||nativeCustomPowerBackend(c.laptop)){const auto result=writeCustomPower(r,c);saveCustomDraft(c,slot,values,r.options);return result;}
    if(customLockedByFanDefault(c))throw std::runtime_error("В Default FANs управление APU недоступно.");
    if(!c.ec)throw std::runtime_error("Custom: device control unavailable");
    const auto result=writeCustomPower(r,c);saveCustomDraft(c,slot,*result.limits,r.options);return result;
}
PowerWriteResult writePowerPreset(const PowerWriteRequest& r,const CustomModeContext& c) {
    const int slot=r.slot;
    if(c.gm6){
        auto applied=c.gm6->useCustomPreset(slot);
        if(!applied)throw std::runtime_error("Custom preset: missing limits");
        return {applied,{}};
    }
    if(nativeCustomPowerBackend(c.laptop))static_cast<void>(c.laptop->read());
    auto library=loadCustomPresetLibrary(c.profileId);
    const auto& preset=*library.slots[slot];
    std::array<int,3> values=preset.power;
    auto options=preset.powerOptions;
    if(preset.powerRecipe){
        if(nativeCustomPowerBackend(c.laptop)){values=c.laptop->customCpuPowerRecipe(preset.powerRecipe-1);options=c.laptop->customPowerOptionRecipe(preset.powerRecipe-1);}
        else if(c.honor)values=honorCustomRecipe(preset.powerRecipe);
        else values=apuPresetLimits(preset.powerRecipe,apuOnBattery());
    }
    if(nativeCustomPowerBackend(c.laptop)){
        // Validate the entire power payload before changing a preset's FANs.
        c.laptop->validateCustomPower(values,options);
        const auto fans=c.nativeFans?c.nativeFans:nativeFixedFanController();if(!fans)throw std::runtime_error("Custom preset: native FAN owner unavailable");
        const auto state=fans->read();if(!state.curves||!state.writable||state.needsRestore)throw std::runtime_error("Custom preset: native FAN state not admitted");
        auto updated=preset;
        if(!preset.curvesReady)for(const auto* device:deviceProfiles())if(device->id==c.profileId)for(int ch=0;ch<2;++ch)for(int i=0;i<device->fanPointCounts[ch];++i){const auto& v=state.curves->draft[ch][i];updated.curves[ch][i]={v.temperature,i?state.curves->draft[ch][i-1].temperature:0,v.duty};}
        FanWriteRequest request{FanResourceOperation::Manual};request.curves=updated.curves;request.origin=FanWriteOrigin::Compound;
        executeFanResource({nullptr,fans.get(),nullptr,c.operation,true,c.profileId},request).requireAccepted();
        try{PowerWriteRequest power{PowerWrite::Custom,values};power.options=options;writeCustomPower(power,c);}catch(const std::exception& error){throw std::runtime_error(std::string("Custom: native FAN curve confirmed, power failed: ")+error.what());}
        updated.power=values;updated.powerOptions=std::move(options);updated.powerRecipe=0;updated.curvesReady=true;
        library.slots[slot]=updated;library.selected=slot;library.active=true;saveCustomPresetLibrary(c.profileId,library);
        return {values,{}};
    }else if(c.honor){
        if(!c.apu)throw std::runtime_error("Custom preset: missing Honor power reader");
        const auto allowed=honorCustomAllowed(values);
        if(values!=allowed)throw std::runtime_error("Custom preset: values outside current Honor limits");
        if(auto fans=nativeFixedFanController()){FanWriteRequest request{FanResourceOperation::Default};request.origin=FanWriteOrigin::Compound;executeFanResource({nullptr,fans.get(),nullptr,c.operation},request).requireAccepted();}
        writeCustomPower({PowerWrite::Custom,values},c);
    }else{
        if(!c.ec||!c.fans)throw std::runtime_error("Custom preset: device control unavailable");
        FanCurveTable curve;for(int i=0;i<16;++i)curve[i]={preset.curves[0][i].up,preset.curves[0][i].duty};
        FanWriteRequest request{FanResourceOperation::Manual};request.ecCurve=curve;request.wait=true;request.origin=FanWriteOrigin::Compound;executeFanResource({nullptr,nullptr,c.fans.get(),c.operation},request).requireAccepted();
        try{writeCustomPower({PowerWrite::Custom,values},c);}catch(const std::exception& e){throw std::runtime_error(std::string("Custom: FAN curve confirmed, power failed: ")+e.what());}
        FanCurveProfiles profiles;std::string error;
        if(!loadFanCurveProfiles(portableSettingsPath("fan-curves.csv"),profiles,error))throw std::runtime_error(error);
        profiles.ecManual=true;profiles.linked=true;profiles.ecManualCurve=curve;profiles.selected.fill(FanCurvePreset::Manual);
        if(!saveFanCurveProfiles(portableSettingsPath("fan-curves.csv"),profiles,error))throw std::runtime_error(error);
        DevicePreferences patch;patch[DeviceSetting::ApuMode]=4;patch[DeviceSetting::ManualPowerEnabled]=1;
        if(c.wujie)patch[DeviceSetting::ApuAutoPower]=0;
        patch[DeviceSetting::ManualStapm]=values[0];patch[DeviceSetting::ManualSlow]=values[1];patch[DeviceSetting::ManualFast]=values[2];
        if(!rememberDevicePreferences(patch,error))throw std::runtime_error(error);
    }
    selectCustomPresetSlot(c.profileId,slot);
    return {values,{}};
}
}
}
