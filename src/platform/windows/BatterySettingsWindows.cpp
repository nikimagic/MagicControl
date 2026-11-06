#include "BatterySettingsWindows.h"
#include "SettingsSnapshotWindows.h"
#include "SettingsPersistenceWindows.h"
#include "BatteryChargeWindows.h"
#include "NativeBatteryWindows.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/BatteryUiPolicy.h"
#include "MagicControl/BatteryAging.h"
#include "MagicControl/SettingsResourceControl.h"
#include "../../protocols/uniwill/x5sp4nag/BatteryContract.h"
#include <algorithm>
#include <stdexcept>

namespace mc::windows {
namespace {
using BatteryRoutes=ResourceControlTable<BatteryWriteRequest,BatterySettingsContext,std::string>;
ResourceControlKey batteryKey(BatteryWrite kind){
    switch(kind){
    case BatteryWrite::Upper:return mc::settingsResourceControlKey(SettingsResourceOperation::ChargeUpper);
    case BatteryWrite::Restart:return mc::settingsResourceControlKey(SettingsResourceOperation::ChargeRestart);
    case BatteryWrite::Policy:return mc::settingsResourceControlKey(SettingsResourceOperation::ChargePolicy);
    case BatteryWrite::Automatic:return mc::settingsResourceControlKey(SettingsResourceOperation::ChargeAutomatic);
    case BatteryWrite::Protection:return mc::settingsResourceControlKey(SettingsResourceOperation::ChargeVoltage);
    case BatteryWrite::NativeTurbo:return mc::settingsResourceControlKey(SettingsResourceOperation::ChargeNativeTurbo);
    case BatteryWrite::UsbPriority:return mc::settingsResourceControlKey(SettingsResourceOperation::UsbPowerPriority);
    case BatteryWrite::Aging:return mc::settingsResourceControlKey(SettingsResourceOperation::ChargeAging);
    case BatteryWrite::View:return mc::settingsResourceControlKey(SettingsResourceOperation::ChargeView);
    case BatteryWrite::Pair:return mc::settingsResourceControlKey(SettingsResourceOperation::ChargePair);
    case BatteryWrite::Preset:case BatteryWrite::PolicyButton:break;
    }throw std::invalid_argument("Unknown battery resource command");
}
std::string writeUpper(const BatteryWriteRequest& r,const BatterySettingsContext& c){
    if(c.native){c.native->setExperimentalChargeLimit(r.value);return {};}
    if(c.laptop){auto actions=nativeBatteryActions();actions.log=c.log;return mc::executeBatteryResource(*c.laptop,r,actions);}
    if(!c.ec)throw std::runtime_error("Battery percentage backend unavailable");
    return applyBatteryChargeLimit(*c.ec,r.value,r.platform7,r.restart,r.requestedPolicy);
}
std::string writeRestart(const BatteryWriteRequest& r,const BatterySettingsContext& c){
    if(c.native){c.native->setChargeRestart(r.value);return {};}
    if(c.laptop){auto actions=nativeBatteryActions();actions.log=c.log;return mc::executeBatteryResource(*c.laptop,r,actions);}
    if(!c.ec)throw std::runtime_error("Battery restart backend unavailable");
    int upper=r.upper;
    if(upper<=0){const int raw=c.ec->readStable(ec::kBatteryChargeControl)&0x7f;upper=raw?raw:100;}
    return applyBatteryChargeLimit(*c.ec,upper,r.platform7,r.value,r.requestedPolicy);
}
std::string writePolicy(const BatteryWriteRequest& r,const BatterySettingsContext& c){
    if(c.native){c.native->setEcPolicy(r.policy);return {};}
    if(c.laptop)throw std::runtime_error("Native battery does not accept Uniwill platform/ROM policy");
    if(!c.ec)throw std::runtime_error("Battery policy backend unavailable");
    return setBatteryEcPolicy(*c.ec,r.policy);
}
std::string writeAutomatic(const BatteryWriteRequest& r,const BatterySettingsContext& c){
    if(!c.ec)throw std::runtime_error("Battery automatic backend unavailable");
    return setBatteryAutomatic(*c.ec,r.enabled);
}
std::string writeView(const BatteryWriteRequest& r,const BatterySettingsContext& c,bool requireWujiePlatform7=false){
    if(r.value!=0&&r.value!=1)throw std::invalid_argument("Unknown battery view");
    if(c.profile.empty())throw std::runtime_error("Battery mode profile unavailable");
    if(r.value==0){
        // One upper100 operation releases percentage control. It preserves
        // actual P7 and restores owned ROM4 to originalFF through its owner.
        if(c.native)c.native->setExperimentalChargeLimit(100);
        else if(c.ec&&c.profile=="wu-jie-15-xpro")
            static_cast<void>(applyBatteryChargeLimit(*c.ec,100,false,-1,{},requireWujiePlatform7?std::optional<unsigned>{7}:std::nullopt));
    }
    std::string error;
    if(!rememberBatteryView(c.profile,r.value,error))throw std::runtime_error(error);
    return r.value==0?"Native battery mode selected; percentage limit released":"Percentage battery view selected";
}
std::string writeProtection(const BatteryWriteRequest& r,const BatterySettingsContext& c){
    if(c.laptop&&c.laptop->chargePolicyId()=="lenovo-legion-kwcn-energy")return mc::executeBatteryResource(*c.laptop,r);
    const auto backend=c.voltage?c.voltage:c.native;
    if(!backend)throw std::runtime_error("Штатный профиль напряжения недоступен");
    if(!batteryProtectionMode(r.value))throw std::invalid_argument("Unknown battery protection mode");
    const auto before=backend->read();
    if(!before.connected||!before.mode||(!before.writable&&!before.voltageTransitionWritable))
        throw std::runtime_error("Штатный профиль: контекст или владелец не подтверждён; настройки не изменены");
    if(r.value!=0&&!before.appPresent)throw std::runtime_error("APP presence отсутствует; профиль не записан");
    static_cast<void>(writeView({BatteryWrite::View,0},c,true));
    backend->set(static_cast<BatteryProtectionMode>(r.value));return {};
}
std::string writeNativeTurbo(const BatteryWriteRequest& r,const BatterySettingsContext& c){
    if(!c.laptop||c.laptop->chargePolicyId()!="huawei-hlyl-wxx9")throw std::runtime_error("Политика батареи для этого OEM backend не подключена");
    return mc::executeBatteryResource(*c.laptop,r);
}
std::string writeUsbPriority(const BatteryWriteRequest& r,const BatterySettingsContext& c){
    if(!c.ec||c.profile!="wu-jie-15-xpro"||(r.value!=0&&r.value!=1))throw std::runtime_error("USB-C priority: unsupported device or value");
    c.ec->ensureCapability(DeviceFeature::BatteryCharge,"uniwill-x5sp4nag");
    const EcAccess::Transaction transaction(*c.ec);
    uniwill::x5sp4nag::requireBatteryIdentity(c.ec->boardVerified(),[&](auto address){return c.ec->readStable(address);});
    c.ec->setUsbPowerPriority(r.value!=0);
    return "USB-C priority bit confirmed; charging and APU response unverified";
}
std::string writePair(const BatteryWriteRequest& r,const BatterySettingsContext& c){
    if(!c.laptop)throw std::runtime_error("Native charge pair backend unavailable");
    return mc::executeBatteryResource(*c.laptop,r);
}
void rememberCapacity(const BatterySettingsContext& c){
    DevicePreferences patch;patch[DeviceSetting::BatteryPercentageEnabled]=1;patch[DeviceSetting::BatteryCardView]=1;std::string error;
    const auto profile=c.profile.empty()&&c.ec?std::string_view("wu-jie-15-xpro"):std::string_view(c.profile);
    if(!rememberDevicePreferences(patch,error,batteryPreferencesPath(profile)))throw std::runtime_error("Режим применён, но не сохранён: "+error);
}
BatteryWriteRequest batteryRequest(const BatterySettingsContext& c,BatteryWriteRequest r){
    if(r.kind==BatteryWrite::PolicyButton)r.kind=c.laptop?BatteryWrite::NativeTurbo:BatteryWrite::Policy;
    if(r.kind!=BatteryWrite::Preset)return r;
    if(r.value==batteryAutomaticIndex&&!c.native&&!c.laptop&&c.ec){
        const EcAccess::Transaction transaction(*c.ec);r.kind=BatteryWrite::Automatic;r.enabled=c.ec->readStable(ec::kBatteryPlatformWmi)!=7;return r;
    }
    if(r.value<0||r.value>=batteryThresholdCount)throw std::runtime_error("Неизвестный процентный предел зарядки");
    r.kind=BatteryWrite::Upper;r.value=batteryThresholdValues[r.value];return r;
}
void rememberObservedBattery(const std::filesystem::path& path,std::uint8_t battery,std::uint8_t platform,std::uint8_t rom){
    std::string error;if(!rememberDevicePreferences(observedBatteryPreferences(battery,platform,rom),error,path))
        throw std::runtime_error("Настройка EC применена, но не сохранена: "+error);
}
void completeBatteryWrite(const BatterySettingsContext& c,const BatteryWriteRequest& r){
    if(r.kind==BatteryWrite::UsbPriority){
        std::string error;if(!savePortableText(portableSettingsPath("wujie-usb-priority.ini"),r.value?"Version=1\nPriority=1\n":"Version=1\nPriority=0\n",error))
            throw std::runtime_error("USB-C priority confirmed but not saved: "+error);
        return;
    }
    // Native controllers own their checked threshold/lower persistence. No
    // duplicate GUI save is allowed; only policy presentation needs its own flag.
    if(c.laptop)return;
    if(c.native){if(r.kind==BatteryWrite::Policy&&r.origin==BatteryWriteOrigin::Explicit)rememberCapacity(c);return;}
    if(!c.ec)return;
    if(r.kind==BatteryWrite::Upper){
        const EcAccess::Transaction transaction(*c.ec);
        rememberObservedBattery(portableSettingsPath("device-settings.ini"),c.ec->readStable(ec::kBatteryChargeControl),
            c.ec->readStable(ec::kBatteryPlatformWmi),c.ec->readStable(0x770));
    }else if(r.kind==BatteryWrite::Restart)rememberChargeRestart("wu-jie-15-xpro",r.value);
    else if((r.kind==BatteryWrite::Policy||r.kind==BatteryWrite::Automatic)&&r.origin==BatteryWriteOrigin::Explicit)rememberCapacity(c);
}
const BatteryRoutes& batteryRoutes(){
    static const auto routes=[] {
        std::vector<BatteryRoutes::Binding> bindings;
        const auto add=[&](BatteryWrite kind,BatteryRoutes::Handler handler){const auto key=batteryKey(kind);bindings.push_back({key.kind,key.instance,key.operation,handler});};
        add(BatteryWrite::Upper,writeUpper);add(BatteryWrite::Restart,writeRestart);add(BatteryWrite::Policy,writePolicy);
        add(BatteryWrite::Automatic,writeAutomatic);add(BatteryWrite::Protection,writeProtection);add(BatteryWrite::NativeTurbo,writeNativeTurbo);
        add(BatteryWrite::UsbPriority,writeUsbPriority);
        add(BatteryWrite::View,[](const BatteryWriteRequest& r,const BatterySettingsContext& c){return writeView(r,c);});add(BatteryWrite::Pair,writePair);
        add(BatteryWrite::Aging,[](const BatteryWriteRequest& r,const BatterySettingsContext& c){
            if(c.ec){setBatteryAgingOverride(*c.ec,r.enabled,r.lifecycle);return std::string{};}
            const auto native=c.native?nativeBatteryAgingAccess():nullptr;
            if(!native)throw std::runtime_error("Battery aging backend unavailable");
            try{setBatteryAgingOverride(*native,r.enabled,r.lifecycle);}
            catch(...){invalidateNativeBatteryAgingObservation();throw;}
            invalidateNativeBatteryAgingObservation();return std::string{};
        });
        return BatteryRoutes(bindings);
    }();return routes;
}
}
std::string executeBatteryResource(const BatterySettingsContext& context,const BatteryWriteRequest& request){
    const auto command=batteryRequest(context,request);
    audit::Scope trace("battery.execute","\"kind\":"+std::to_string(static_cast<int>(command.kind))+",\"origin\":"+audit::quote(command.origin==BatteryWriteOrigin::Restore?"restore":"user"));
    const auto result=batteryRoutes().execute(batteryKey(command.kind),command,context);
    completeBatteryWrite(context,command);
    if(context.log&&!result.empty())context.log("Battery command confirmed: "+result);
    trace.finish("backend_completed");return result;
}
std::optional<BatteryWriteRequest> savedBatteryWriteRequest(const DevicePreferences& saved){
    using S=DeviceSetting;
    if(saved[S::BatteryPercentageEnabled]!=0&&!saved[S::ChargeLimit])return {};
    BatteryWriteRequest request=saved[S::BatteryPercentageEnabled]==0?BatteryWriteRequest{BatteryWrite::View,0}:
        BatteryWriteRequest{BatteryWrite::Upper,*saved[S::ChargeLimit]};
    // Restore percentage intent through the actual platform, not legacy ChargeAuto.
    request.origin=BatteryWriteOrigin::Restore;return request;
}
bool honorBatteryBackend(const LaptopControl* control) noexcept {
    return nativeBatteryPolicySupported(control);
}
bool batteryPolicyActive(const gui::SettingsSnapshot& s,bool native) noexcept {
    if(native)return s.nativeBattery.enforcementOn.value_or(false);
    return s.valid&&s.batteryPlatform==7&&s.batteryRom==4;
}
int selectedBatteryThreshold(const gui::SettingsSnapshot& s,bool native,bool oem,std::string_view profile) noexcept {
    if(native&&nativeEcPolicyProfile(profile)&&s.nativeBattery.connected&&s.nativeBattery.platformRaw&&s.nativeBattery.romIdRaw){
        const auto policy=observedBatteryEcPolicy(*s.nativeBattery.platformRaw,*s.nativeBattery.romIdRaw);
        if(policy&&*policy==BatteryEcPolicy::Platform7Original)return 0;
    }
    if(!oem&&!native&&s.valid&&s.batteryPlatform==7&&s.batteryRom==255)return 0;
    if(native){
        const auto& b=s.nativeBattery;
        if(!b.connected||!b.upperRaw)return -1;
        const int raw=*b.upperRaw&0x7f;
        const int upper=raw?raw:b.configuredUpper==100?100:-1;
        for(int i=0;i<batteryThresholdCount;++i)if(batteryThresholdValues[i]==upper)return i;
        return -1;
    }
    if(oem&&(!s.oem.chargeEnd||*s.oem.chargeEnd==0))return -1;
    if(!s.valid)return -1;
    const int value=s.battery&ec::kBatteryChargeLimitMask;
    if(!oem&&s.batteryPlatform!=4&&s.batteryPlatform!=7)return -1;
    if(value==0||value==100)return 0;
    return value==95?1:value==85?2:value==60?3:-1;
}
std::wstring batteryPortSummary(const gui::SettingsSnapshot& s,bool preview,bool native,bool gm,bool oem,bool hasEc,
                                const std::function<std::wstring()>& fallback){
    if(preview)return L"Просмотр профиля";
    if(native&&s.nativeBattery.chargerRaw&&s.batteryTelemetry.onlineKnown&&s.batteryTelemetry.online&&!s.batteryTelemetry.dynamicStale){
        // Independently reviewed GM6/HYDROC masks; no guessed port numbers.
        const auto raw=*s.nativeBattery.chargerRaw;std::wstring source;
        if(raw&1)source=L"DC (круглый разъём)";
        if(raw&0x36){
            if(!source.empty())source+=L" + ";source+=L"USB-C (порт не определён)";
            const auto a=s.nativeBattery.typeCPowerA,b=s.nativeBattery.typeCPowerB;
            if(a&&b){const int rating=std::max<int>(*a,*b);if(rating>0&&rating<255)source+=L" · "+std::to_wstring(rating)+L" Вт";}
        }
        if(!source.empty())return L"Порт зарядки: "+source;
    }
    if(gm&&s.batteryTelemetry.onlineKnown&&s.batteryTelemetry.online&&!s.batteryTelemetry.dynamicStale)
        return L"Разъём питания: DC (круглый разъём)";
    if(gm||oem||!hasEc)return fallback();
    if(s.batteryTelemetry.onlineKnown&&!s.batteryTelemetry.online&&!s.batteryTelemetry.dynamicStale)
        return L"Порт зарядки: Питание не подключено.";
    if(!s.valid||!s.chargerRead)return L"Порт зарядки: данные EC недоступны.";
    const bool roundSocket=(s.typecStatus&0x01u)!=0,port1=(s.typecStatus&0x12u)!=0,port2=(s.typecStatus&0x24u)!=0;
    const int typecPower=std::max<int>(s.typecPowerA,s.typecPowerB);std::wstring source;
    if(roundSocket)source=L"DC (круглый разъём)";
    if(port1||port2||typecPower>0){
        if(!source.empty())source+=L" + ";
        source+=port1&&port2?L"USB-C 1 + USB-C 2":port1?L"USB-C 1":port2?L"USB-C 2":L"USB-C (порт не определён)";
        source+=typecPower>0?L" · "+std::to_wstring(typecPower)+L" Вт":L" · мощность не определена";
    }
    if(source.empty())return L"Порт зарядки: внешнее питание не подтверждено EC.";
    return L"Порт зарядки: "+source;
}
BatteryEditorAvailability batteryEditorAvailability(const BatteryEditorInput& input) noexcept{
    const bool ready=!input.preview&&(!input.busy||input.reading)&&!input.interactiveBusy;
    return {ready&&(input.native||input.ec),ready&&(input.native||input.ec)&&input.hasObservation,
        ready&&(input.native?(!input.gmAmd&&input.nativePercentageWritable):input.oem?input.oemChargeWritable:input.ec&&input.snapshotValid),
        input.native?95:99};
}
bool nativeVoltageButtonEnabled(const NativeVoltageButtonInput& c,int mode,const std::function<bool()>& interactiveBusy){
    return !c.preview&&(!c.busy||c.reading)&&c.backend&&c.writable&&c.context&&
        (mode==0||c.appPresent)&&!interactiveBusy();
}
BatteryButtonAvailability gmBatteryButtonAvailability(bool idle,bool queued,bool percentageWritable,bool enforcementWritable) noexcept{
    BatteryButtonAvailability out;out.percentage=idle&&!queued&&percentageWritable;
    out.policy.fill(idle&&!queued&&enforcementWritable);return out;
}
BatteryButtonAvailability honorBatteryButtonAvailability(bool idle,bool chargeWritable,bool turboWritable,const std::function<bool()>& onBattery){
    BatteryButtonAvailability out;out.percentage=idle&&chargeWritable;
    for(auto& value:out.policy)value=idle&&turboWritable&&!onBattery();return out;
}
BatteryButtonAvailability ecBatteryButtonAvailability(const EcBatteryButtons& input) noexcept{
    BatteryButtonAvailability out;
    if(input.native){const bool available=(!input.busy||(input.reading&&!input.queued))&&!input.interactiveBusy;
        out.percentage=input.valid&&available&&input.percentageWritable;out.policy.fill(available&&input.enforcementWritable);
    }else{out.percentage=input.enabled;out.policy.fill(input.enabled);}
    return out;
}
}
