#include "SettingsActionWindows.h"
#include "CustomModeWindows.h"
#include "MagicControl/SettingsResourceControl.h"
#include "MagicControl/BatteryUiPolicy.h"
#include <stdexcept>
#include <utility>

namespace mc::windows {
SettingsCommandSemantics settingsActionSemantics(const std::optional<SettingsAction>& action,bool gmNativePower,bool passive) noexcept{
    SettingsCommandSemantics s;if(!action)return s;
    using K=SettingsActionKind;using C=gui::SettingsRememberChoice;const auto kind=action->kind;
    s.quickProfile=gmNativePower&&!passive&&((kind==K::StockPower&&action->value>=0&&action->value<5)||kind==K::CustomEditor||kind==K::CustomPreset);
    s.nativeBatteryMode=!passive&&kind==K::BatteryVoltage;
    // BatteryUpper carries a button index; the trailing Automatic entry is
    // not a percentage command and must not persist the current percentage.
    s.batteryUpper=kind==K::BatteryUpper&&action->value>=0&&action->value<batteryThresholdCount;
    s.batteryPolicy=kind==K::BatteryPolicy;
    s.cycleTrigger=(kind==K::LightingEffect&&action->value==3)||(kind==K::LightingCycle&&action->cycleIntent!=LightingCycleIntent::Color);
    s.colorChoice=kind==K::LightingColor||(kind==K::LightingCycle&&action->cycleIntent==LightingCycleIntent::Color);
    s.lightingChoice=kind==K::LightingEffect||s.colorChoice||kind==K::LightingLevel||kind==K::LightingNative||kind==K::LightingCustomColor||(kind==K::LightingCycle&&action->cycleIntent==LightingCycleIntent::Effect);
    switch(kind){
    case K::LightingLevel:s.remember=C::LightingLevel;s.rememberIndex=action->value;break;
    case K::LightingEffect:s.remember=C::LightingEffect;break;
    case K::LightingColor:s.remember=C::LightingColor;break;
    case K::LightingCustomColor:s.remember=C::LightingCustomColor;break;
    case K::LightingNative:s.remember=C::LightingNative;break;
    case K::LightingCycle:s.remember=action->cycleIntent==LightingCycleIntent::Color?C::LightingColor:action->cycleIntent==LightingCycleIntent::Effect?C::LightingEffect:C::LightingCycle;break;
    case K::BatteryUpper:break; // Shared battery entry owns its confirmed completion.
    case K::StockPower:s.remember=C::Performance;s.rememberIndex=action->value;break;
    case K::CustomEditor:s.remember=C::ManualPower;break;
    default:break;
    }
    return s;
}
ResourceControlKey settingsResourceControlKey(const SettingsAction& action){
    using K=SettingsActionKind;using O=SettingsResourceOperation;
    switch(action.kind){
    case K::BatteryUpper:return mc::settingsResourceControlKey(O::ChargeUpper);
    case K::BatteryRestart:return mc::settingsResourceControlKey(O::ChargeRestart);
    case K::BatteryPolicy:return mc::settingsResourceControlKey(O::ChargePolicy);
    case K::BatteryVoltage:return mc::settingsResourceControlKey(O::ChargeVoltage);
    case K::BatteryAging:return mc::settingsResourceControlKey(O::ChargeAging);
    case K::BatteryView:return mc::settingsResourceControlKey(O::ChargeView);
    case K::UsbPowerPriority:return mc::settingsResourceControlKey(O::UsbPowerPriority);
    case K::StockPower:return mc::settingsResourceControlKey(O::PowerStock);
    case K::CustomPreset:return mc::settingsResourceControlKey(O::PowerCustomPreset);
    case K::CustomEditor:return mc::settingsResourceControlKey(O::PowerCustomEditor);
    case K::CustomBase:return mc::settingsResourceControlKey(O::PowerCustomBase);
    case K::LightingLevel:return mc::settingsResourceControlKey(O::LightLevel);
    case K::LightingEffect:return mc::settingsResourceControlKey(O::LightEffect);
    case K::LightingColor:return mc::settingsResourceControlKey(O::LightColor);
    case K::LightingCustomColor:return mc::settingsResourceControlKey(O::LightCustomColor);
    case K::LightingNative:return mc::settingsResourceControlKey(O::LightNative);
    case K::LightingTimeout:return mc::settingsResourceControlKey(O::LightTimeout);
    case K::LightingCycle:return mc::settingsResourceControlKey(O::LightCycle);
    case K::LightingZone:return mc::settingsResourceControlKey(O::LightZone);
    case K::EcLogger:return mc::settingsResourceControlKey(action.logger&&action.logger->writing?O::EcWrite:O::EcRead);
    }
    throw std::invalid_argument("SETs: неизвестная команда");
}

namespace {
using Table=ResourceControlTable<SettingsAction,SettingsActionServices,SettingsActionResult>;
SettingsActionResult upper(const SettingsAction& a,const SettingsActionServices& s){executeBatteryResource(s.battery(),{BatteryWrite::Preset,a.value});SettingsActionResult r;r.batteryView=1;return r;}
SettingsActionResult view(const SettingsAction& a,const SettingsActionServices& s){executeBatteryResource(s.battery(),{BatteryWrite::View,a.value});SettingsActionResult r;r.batteryView=a.value;return r;}
SettingsActionResult restart(const SettingsAction& a,const SettingsActionServices& s){executeBatteryResource(s.battery(),{BatteryWrite::Restart,a.value});return {};}
SettingsActionResult policy(const SettingsAction& a,const SettingsActionServices& s){BatteryWriteRequest r{BatteryWrite::PolicyButton};r.policy=a.policy;r.enabled=a.enabled;executeBatteryResource(s.battery(),r);return {};}
SettingsActionResult voltage(const SettingsAction& a,const SettingsActionServices& s){executeBatteryResource(s.battery(),{BatteryWrite::Protection,a.value});SettingsActionResult r;r.batteryView=0;return r;}
SettingsActionResult aging(const SettingsAction& a,const SettingsActionServices& s){BatteryWriteRequest r{BatteryWrite::Aging};r.enabled=a.enabled;executeBatteryResource(s.battery(),r);return {};}
SettingsActionResult usbPriority(const SettingsAction& a,const SettingsActionServices& s){executeBatteryResource(s.battery(),{BatteryWrite::UsbPriority,a.value});return {};}
SettingsActionResult stock(const SettingsAction& a,const SettingsActionServices& s){
    if(a.value<0||a.value>4)throw std::invalid_argument("Unknown CPU power button");
    const auto c=s.custom();
    const PowerWriteRequest request{a.value==0?PowerWrite::Economy:a.value==4?PowerWrite::Automatic:PowerWrite::Stock,{},a.value-1};
    const auto result=executePowerResource(c,request);
    SettingsActionResult out;if(c.gm6)out.gmExpected=result.limits;return out;
}
SettingsActionResult level(const SettingsAction& a,const SettingsActionServices& s){gui::executeLightingResource(s.lighting(),{LightingWrite::Level,a.value});return {};}
SettingsActionResult effect(const SettingsAction& a,const SettingsActionServices& s){gui::executeLightingResource(s.lighting(),{LightingWrite::Effect,a.value});return {};}
SettingsActionResult color(const SettingsAction& a,const SettingsActionServices& s){gui::executeLightingResource(s.lighting(),{LightingWrite::Palette,a.value});return {};}
SettingsActionResult customColor(const SettingsAction& a,const SettingsActionServices& s){LightingWriteRequest r{LightingWrite::CustomColor};r.color=a.color;gui::executeLightingResource(s.lighting(),r);return {};}
SettingsActionResult nativeLight(const SettingsAction&,const SettingsActionServices& s){gui::executeLightingResource(s.lighting(),{LightingWrite::Native});return {};}
SettingsActionResult timeout(const SettingsAction& a,const SettingsActionServices& s){gui::executeLightingResource(s.lighting(),{LightingWrite::Timeout,a.value});return {};}
SettingsActionResult cycle(const SettingsAction& a,const SettingsActionServices& s){gui::executeLightingResource(s.lighting(),LightingWriteRequest::forCycle(a.cycle));return {};}
SettingsActionResult zone(const SettingsAction& a,const SettingsActionServices& s){gui::executeLightingResource(s.lighting(),LightingWriteRequest::forZone(a.zone));return {};}
SettingsActionResult preset(const SettingsAction& a,const SettingsActionServices& s){const auto c=s.custom();const auto v=executePowerResource(c,{PowerWrite::Preset,{},a.value});return {v.limits,bool(c.gm6),{}};}
SettingsActionResult customBase(const SettingsAction& a,const SettingsActionServices& s){executePowerResource(s.custom(),{PowerWrite::CustomBase,{},a.value});return {};}
SettingsActionResult editor(const SettingsAction& a,const SettingsActionServices& s){
    if(!a.customPlan)throw std::invalid_argument("Custom: неполный запрос редактора");
    SettingsActionResult result;
    executeCustomEditorApply(*a.customPlan,{[&](const auto& values){PowerWriteRequest request{PowerWrite::CustomEditor,values,a.value};request.options=a.powerOptions;return executePowerResource(s.custom(),request).limits.value();},
        [&](const auto& values){result.gmExpected=values;},s.log});return result;
}
SettingsActionResult logger(const SettingsAction& a,const SettingsActionServices& s){
    if(!a.logger||!s.loggerTouched||!s.loggerObservation)throw std::invalid_argument("SETs: неполный запрос логгера");
    *s.loggerTouched=true;const auto c=s.battery();gui::executeEcLogger(*a.logger,c.native,c.ec,*s.loggerObservation);return {};
}
const Table& settingsRoutes(){
    static const auto routes=[] {
        using K=SettingsActionKind;
        const std::array<std::pair<K,Table::Handler>,19> actions{{
            {K::BatteryView,view},
            {K::BatteryAging,aging},
            {K::BatteryUpper,upper},{K::BatteryRestart,restart},{K::BatteryPolicy,policy},{K::BatteryVoltage,voltage},{K::UsbPowerPriority,usbPriority},
            {K::StockPower,stock},{K::CustomPreset,preset},{K::CustomEditor,editor},{K::CustomBase,customBase},
            {K::LightingLevel,level},{K::LightingEffect,effect},{K::LightingColor,color},{K::LightingCustomColor,customColor},
            {K::LightingNative,nativeLight},{K::LightingTimeout,timeout},{K::LightingCycle,cycle},{K::LightingZone,zone}
        }};
        std::vector<Table::Binding> bindings;bindings.reserve(18);
        const auto add=[&](const ResourceControlKey& key,Table::Handler handler){bindings.push_back({key.kind,key.instance,key.operation,handler});};
        for(const auto& [kind,handler]:actions)add(settingsResourceControlKey(SettingsAction{kind}),handler);
        add(mc::settingsResourceControlKey(SettingsResourceOperation::EcRead),logger);
        add(mc::settingsResourceControlKey(SettingsResourceOperation::EcWrite),logger);
        return Table(bindings);
    }();return routes;
}
}
bool settingsResourceRouteRegistered(const ResourceControlKey& key){return settingsRoutes().hasRoute(key);}
SettingsActionResult executeSettingsAction(const SettingsAction& action,const SettingsActionServices& services){
    return settingsRoutes().execute(settingsResourceControlKey(action),action,services);
}
}
