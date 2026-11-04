#pragma once
#include "PortableSettings.h"
#include "BatteryEcPolicy.h"
#include <array>
#include <optional>
#include <string_view>
namespace mc {
inline constexpr std::array<int,4> batteryThresholdValues{100,95,85,60};
inline constexpr int batteryThresholdCount=static_cast<int>(batteryThresholdValues.size());
inline constexpr int batteryAutomaticIndex=batteryThresholdCount;
// Capture/migration recognizes current presets plus the stored legacy10 value.
// Raw0 is the firmware encoding of100; this does not authorize a device write.
inline std::optional<int> capturedBatteryChargeLimit(int raw)noexcept{
    if(raw==0)return 100;
    if(raw==10)return 10;
    for(const auto value:batteryThresholdValues)if(raw==value)return value;
    return {};
}
inline std::optional<std::filesystem::path> batteryCardPreferencesPath(std::string_view profile) noexcept{
    try{return batteryPreferencesPath(profile);}catch(...){return {};}
}
// Pure projection of an already confirmed snapshot; never write admission.
inline DevicePreferences observedBatteryPreferences(std::uint8_t battery,std::uint8_t platform,std::uint8_t rom){
    DevicePreferences patch;const int limit=battery&0x7f;
    patch[DeviceSetting::ChargeLimit]=limit?limit:100;
    patch[DeviceSetting::BatteryPercentageEnabled]=1;patch[DeviceSetting::BatteryCardView]=1;
    if(const auto policy=observedBatteryEcPolicy(platform,rom))patch[DeviceSetting::ChargeAuto]=static_cast<int>(*policy);
    return patch;
}
inline bool rememberBatteryView(std::string_view profile,int view,std::string& error){
    if(view!=0&&view!=1){error="Unknown battery view";return false;}
    const auto path=batteryCardPreferencesPath(profile);if(!path)return true;
    DevicePreferences patch;patch[DeviceSetting::BatteryCardView]=view;
    if(view==0&&(nativeEcPolicyProfile(profile)||profile=="machenike-l16"))patch[DeviceSetting::BatteryPercentageEnabled]=0;
    return rememberDevicePreferences(patch,error,*path);
}
enum class BatteryCardView {NativeVoltage=0,Capacity=1};
// Unknown or failed observations must not be treated as proof of absence.
inline bool batteryCardVisible(bool declaredAbsent,std::optional<bool> osPresent,bool osStale,
                               std::optional<bool> ecPresent,bool ecFresh) noexcept {
    if(osPresent==true||(ecFresh&&ecPresent==true))return true;
    if((osPresent==false&&!osStale)||(ecFresh&&ecPresent==false&&!osPresent.has_value()))return false;
    return !declaredAbsent;
}
inline BatteryCardView initialBatteryCardView(std::string_view profile) noexcept{
    if(profile=="lenovo-legion-kwcn")return BatteryCardView::NativeVoltage;
    try{
    const auto path=batteryCardPreferencesPath(profile);if(!path)return BatteryCardView::NativeVoltage;
    DevicePreferences saved;std::string error;
    if(loadDevicePreferences(*path,saved,error)){
        if(const auto view=saved[DeviceSetting::BatteryCardView])return *view?BatteryCardView::Capacity:BatteryCardView::NativeVoltage;
        // Preserve the existing percentage presentation for legacy users; the
        // dropdown's FIRST entry is always the native voltage mode.
        if(saved[DeviceSetting::BatteryPercentageEnabled]==0&&saved[DeviceSetting::BatteryProtection])return BatteryCardView::NativeVoltage;
        if(saved[DeviceSetting::ChargeLimit])return BatteryCardView::Capacity;
    }}catch(...){}
    return BatteryCardView::NativeVoltage;
}

}
