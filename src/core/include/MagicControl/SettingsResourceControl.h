#pragma once
#include "ResourceControl.h"

namespace mc {
struct TraySettingsState {
    bool ready{},performanceReady{},keyboardSupported{},turboAvailable{},batteryAuto{},apuPolicyReady{};
    int battery{-1},performance{-1},brightness{-1};
};
enum class TraySetting {Battery,Performance,Brightness};

enum class SettingsResourceOperation {
    ChargeUpper,ChargeRestart,ChargePair,ChargePolicy,ChargeVoltage,ChargeAging,
    PowerStock,PowerCustomPreset,PowerCustomEditor,
    LightLevel,LightEffect,LightColor,LightCustomColor,LightNative,LightTimeout,LightCycle,LightZone,LightUpdate,
    EcRead,EcWrite,
    ChargeAutomatic,ChargeNativeTurbo,PowerCustomWrite,PowerCustomSelect,PowerAutomatic,PowerEconomy,PowerAllowed,UsbPowerPriority,ChargeView,PowerCustomBase
};
// Operations describe intent on the selected resource. Device-specific data,
// validation, firmware commands and access checks remain with the adapter.
inline ResourceControlKey settingsResourceControlKey(SettingsResourceOperation action){
    using O=SettingsResourceOperation;using R=ResourceKind;
    switch(action){
    case O::ChargeUpper:return {R::Battery,"active","charge.upper"};
    case O::ChargeRestart:return {R::Battery,"active","charge.restart"};
    case O::ChargePair:return {R::Battery,"active","charge.pair"};
    case O::ChargePolicy:return {R::Battery,"active","charge.policy"};
    case O::ChargeVoltage:return {R::Battery,"active","charge.voltage"};
    case O::ChargeAging:return {R::Battery,"active","charge.aging"};
    case O::PowerStock:return {R::Processor,"active","power.stock"};
    case O::PowerCustomPreset:return {R::Processor,"active","power.custom.preset"};
    case O::PowerCustomEditor:return {R::Processor,"active","power.custom.editor"};
    case O::LightLevel:return {R::Lighting,"active","lighting.level"};
    case O::LightEffect:return {R::Lighting,"active","lighting.effect"};
    case O::LightColor:return {R::Lighting,"active","lighting.color"};
    case O::LightCustomColor:return {R::Lighting,"active","lighting.custom-color"};
    case O::LightNative:return {R::Lighting,"active","lighting.native"};
    case O::LightTimeout:return {R::Lighting,"active","lighting.timeout"};
    case O::LightCycle:return {R::Lighting,"active","lighting.cycle"};
    case O::LightZone:return {R::Lighting,"active","lighting.zone"};
    case O::LightUpdate:return {R::Lighting,"active","lighting.update"};
    case O::EcRead:return {R::Other,"ec","ec.read"};
    case O::EcWrite:return {R::Other,"ec","ec.write"};
    case O::ChargeAutomatic:return {R::Battery,"active","charge.automatic"};
    case O::ChargeNativeTurbo:return {R::Battery,"active","charge.native-turbo"};
    case O::PowerCustomWrite:return {R::Processor,"active","power.custom.write"};
    case O::PowerCustomBase:return {R::Processor,"active","power.custom.base"};
    case O::PowerCustomSelect:return {R::Processor,"active","power.custom.select"};
    case O::PowerAutomatic:return {R::Processor,"active","power.automatic"};
    case O::PowerEconomy:return {R::Processor,"active","power.economy"};
    case O::PowerAllowed:return {R::Processor,"active","power.allowed"};
    case O::UsbPowerPriority:return {R::Battery,"active","power.usb-priority"};
    case O::ChargeView:return {R::Battery,"active","charge.view"};
    }
    throw std::invalid_argument("SETs: unknown resource operation");
}
}
