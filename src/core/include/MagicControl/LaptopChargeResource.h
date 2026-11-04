#pragma once
#include "LaptopControl.h"
#include "BatteryEcPolicy.h"
#include "PortableSettings.h"
#include <algorithm>
#include <functional>

namespace mc {
// One data contract for GUI/CLI/lifecycle. These values describe intent and
// never grant access to a transport, firmware layout or platform/ROM policy.
enum class BatteryWrite {Upper,Restart,Policy,Automatic,Protection,NativeTurbo,UsbPriority,Aging,View,Pair,Preset,PolicyButton};
enum class BatteryWriteOrigin {Explicit,Restore};
struct BatteryWriteRequest {
    BatteryWrite kind{BatteryWrite::Upper};int value{-1};bool platform7{};int upper{};int restart{-1};
    BatteryEcPolicy policy{BatteryEcPolicy::Platform4Original};bool enabled{};
    std::optional<BatteryEcPolicy> requestedPolicy{};
    bool lifecycle{};
    BatteryWriteOrigin origin{BatteryWriteOrigin::Explicit};
    static BatteryWriteRequest pair(int start,int end){BatteryWriteRequest r{BatteryWrite::Pair,end};r.restart=start;return r;}
    static BatteryWriteRequest native(int upper,int lower){
        BatteryWriteRequest r{upper>=0?BatteryWrite::Upper:BatteryWrite::Restart,upper>=0?upper:lower};
        r.restart=upper>=0?lower:-1;return r;
    }
};
struct NativeBatteryActions {
    std::function<int(int)> selectedRestart{};
    std::function<void(int)> rememberRestart{};
    std::function<void()> rememberCapacity{};
    std::function<void(std::string)> log{};
};
inline bool nativeBatteryPolicySupported(const LaptopControl* control)noexcept{
    return control&&control->chargePolicyId()=="huawei-hlyl-wxx9";
}
inline NativeBatteryActions nativeBatteryActions(){
    return {[](int fallback){return selectedChargeRestart("huawei-hlyl-wxx9",fallback);},
        [](int value){rememberChargeRestart("huawei-hlyl-wxx9",value);},
        []{DevicePreferences patch;patch[DeviceSetting::BatteryPercentageEnabled]=1;patch[DeviceSetting::BatteryCardView]=1;std::string error;
            if(!rememberDevicePreferences(patch,error,batteryPreferencesPath("huawei-hlyl-wxx9")))throw std::runtime_error("Режим применён, но не сохранён: "+error);},{}};
}
// Portable native policy. The exact backend retains identity/permission,
// stable readback and owned rollback. No Uniwill byte map is inferred here.
inline std::string executeBatteryResource(LaptopControl& control,const BatteryWriteRequest& request,
        const NativeBatteryActions& actions=nativeBatteryActions()){
    if(control.chargePolicyId()=="lenovo-legion-kwcn-energy"){
        if(request.kind!=BatteryWrite::Protection||request.value<0||request.value>2||request.platform7||request.requestedPolicy)
            throw std::invalid_argument("Lenovo: доступен только штатный режим зарядки Normal / Conservation / Rapid");
        control.withChargePolicyTransaction([&]{control.setChargeMode(request.value);});
        return "Lenovo: режим зарядки подтверждён чтением";
    }
    if(!nativeBatteryPolicySupported(&control))throw std::runtime_error("Политика батареи для этого backend не подключена");
    if(request.platform7||request.requestedPolicy)throw std::invalid_argument("Native battery does not accept Uniwill platform/ROM policy");
    if(request.kind==BatteryWrite::NativeTurbo){control.setNativeTurbo(request.enabled);return {};}
    if(request.kind!=BatteryWrite::Upper&&request.kind!=BatteryWrite::Restart&&request.kind!=BatteryWrite::Pair)
        throw std::invalid_argument("Battery operation is unavailable through this native transport");
    if(request.kind==BatteryWrite::Upper&&(request.value<1||request.value>100))throw std::invalid_argument("Верхний предел должен быть1–100%");
    std::string message;
    control.withChargePolicyTransaction([&]{
        if(request.kind==BatteryWrite::Pair){
            // Expert raw pair keeps native0/100 normalization and backend bounds;
            // it is not a request to overwrite the user's saved restart value.
            control.setChargeThresholds(request.restart,request.value);message="Native charge pair confirmed";return;
        }
        const auto actual=control.readTelemetry();
        const int end=request.kind==BatteryWrite::Upper?request.value:actual.chargeEnd.value_or(0);
        if(!actual.chargeWritable||end<1)throw std::runtime_error("Сначала выберите верхний предел зарядки");
        const bool explicitRestart=request.kind==BatteryWrite::Restart||request.restart>=0;
        const int requestedStart=request.kind==BatteryWrite::Restart?request.value:request.restart;
        const int selected=explicitRestart?requestedStart:end==10?5:actions.selectedRestart(end-5);
        const int start=!explicitRestart&&end==60?std::min(selected,55):selected;
        if(start<1||start>=end)throw std::runtime_error("Нижний порог должен быть ниже верхнего");
        control.setChargeThresholds(start,end);
        if(request.origin==BatteryWriteOrigin::Explicit){
            if((explicitRestart||end==10)&&actions.rememberRestart)actions.rememberRestart(start);
            if(request.kind==BatteryWrite::Upper&&actions.rememberCapacity)actions.rememberCapacity();
        }
        if(actions.log&&request.kind==BatteryWrite::Upper)actions.log("Native charge thresholds confirmed: "+std::to_string(start)+"/"+std::to_string(end));
        message=explicitRestart?(request.origin==BatteryWriteOrigin::Explicit?"Нижний порог записан и сохранён.":"Нижний порог подтверждён чтением."):
            "Верхний предел подтверждён чтением.";
    });
    return message;
}
}
