#include "SettingsSnapshotWindows.h"
#include "ApuPowerPolicyWindows.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include "MagicControl/ControlAudit.h"
#include "IntelTelemetryWindows.h"
#include "LightingControlWindows.h"
#include "LightingOwnershipWindows.h"
#include <windows.h>
#include <format>
#include <stdexcept>

namespace mc::gui {
SettingsDisplayTransition transitionSettingsDisplay(SettingsSnapshot& state,SettingsSnapshot& pending,
    bool clearActual,bool superseded,bool retainSingleZone,SingleZoneModel model,bool& loaded,
    EcBatteryDisplayCache& batteryCache,LaptopDisplayCache& oemCache,KeyboardDisplayCache& keyboardCache){
    SettingsDisplayTransition result{clearActual||(pending.valid&&pending!=state),
        pending.valid&&!clearActual&&!superseded,pending.valid};
    if(clearActual){
        state.valid=false;state.keyboardIdle.reset();state.keyboardRetained=state.keyboardSupported;state.keyboardWritable=false;
        state.gm6.connected=state.gm6.writable=false;state.gm6.cpuPresetWritable=false;
        state.oem=oemCache.update({});
        EcBatteryReading unavailable;unavailable.supported=state.ecBattery.supported;
        unavailable.profileId=state.ecBattery.profileId;unavailable.status="Текущее чтение недоступно";
        state.ecBattery=batteryCache.update(std::move(unavailable));
        state.nativeBattery.connected=state.nativeBattery.writable=state.nativeBattery.percentageWritable=state.nativeBattery.enforcementWritable=false;
        state.batteryTelemetry.dynamicStale=true;state.batteryTelemetry.healthStale=state.batteryTelemetry.healthPercent.has_value();
        state.apu.stale=true;state.power.accepted={};state.power.acceptedValid=false;loaded=false;
    }
    if(pending.valid){
        pending.ecBattery=batteryCache.update(std::move(pending.ecBattery));pending.oem=oemCache.update(std::move(pending.oem));
        if(retainSingleZone){std::optional<KeyboardBacklightState> raw;
            if(pending.keyboardSupported){KeyboardBacklightState k;k.model=model;k.supported=true;k.keyboard=pending.keyboard;
                k.effect=pending.effect;k.breathing=pending.breathing;k.color=pending.color;k.pulseRaw=pending.keyboardPulseRaw;raw=k;}
            if(const auto shown=keyboardCache.update(raw);shown&&shown->retainedForDisplay)collectSettingsSingleZone(pending,*shown);
        }
        state=pending;loaded=true;pending.valid=false;
    }
    return result;
}
namespace {
std::string batteryAgingAudit(const BatteryAgingStatus& s){
    if(!s.reading&&!s.enabled&&s.error.empty())return "null";
    return std::string("{\"selected\":")+(s.enabled?(*s.enabled?"true":"false"):"null")+
        ",\"writable\":"+(s.writable?"true":"false")+",\"authoritative\":"+(s.authoritative?"true":"false")+
        ",\"retained\":"+(s.retainedForDisplay?"true":"false")+",\"sampled_at_ms\":"+std::to_string(s.sampledAtMs)+
        ",\"real_history\":"+(s.realHistory?std::to_string(*s.realHistory):"null")+
        (s.reading&&!s.enabled?",\"date_raw\":"+std::to_string(s.reading->date)+",\"cached_months\":"+std::to_string(s.reading->months)+",\"applied_months\":"+std::to_string(s.reading->appliedMonths):"")+
        ",\"status\":"+audit::quote(s.error)+"}";
}
}
std::string settingsSnapshotAudit(const SettingsSnapshot& s){
    return "{\"valid\":"+std::string(s.valid?"true":"false")+",\"battery_aging\":"+batteryAgingAudit(s.ecBattery.aging)+",\"keyboard_supported\":"+(s.keyboardSupported?"true":"false")+",\"raw_78C\":"+(s.keyboardSupported?std::to_string(s.keyboard):"null")+",\"raw_767\":"+(s.keyboardSupported?std::to_string(s.effect):"null")+",\"pulse_7C5_low3\":"+(s.keyboardSupported?std::to_string(s.breathing):"null")+",\"raw_rgb\":"+(s.keyboardSupported?audit::rgb(s.color):"null")+",\"gm_connected\":"+(s.gm6.connected?"true":"false")+",\"gm_status\":"+audit::quote(s.gm6.status)+",\"oem\":"+audit::laptop(s.oem)+
        ",\"usb_priority\":{\"raw\":"+(s.usbPriorityStatus?std::to_string(s.usbPriorityStatus->raw):"null")+
        ",\"writable\":"+(s.usbPriorityStatus&&s.usbPriorityStatus->writable?"true":"false")+
        ",\"status\":"+audit::quote(s.usbPriorityStatus?s.usbPriorityStatus->reason:"unavailable")+"}"+
        ",\"keyboard_idle\":"+audit::keyboardIdle(s.keyboardIdle)+",\"ec_power\":"+audit::ecPower(s.power)+",\"smu_comparison\":{\"stapm\":"+audit::optional(s.apu.stapmLimitWatts)+",\"slow\":"+audit::optional(s.apu.slowLimitWatts)+",\"fast\":"+audit::optional(s.apu.fastLimitWatts)+",\"stale\":"+(s.apu.stale?"true":"false")+"}}";
}
std::array<std::optional<double>,3> observedSettingsPowerLimits(const SettingsSnapshot& s,bool gm,bool wujie){
    if(s.oem.connected&&s.oem.customCpuPowerLimits)return {double((*s.oem.customCpuPowerLimits)[0]),double((*s.oem.customCpuPowerLimits)[1]),double((*s.oem.customCpuPowerLimits)[2])};
    if(gm)return {s.gm6.connected?std::optional<double>(s.gm6.limits[0]):std::nullopt,
        s.gm6.connected?std::optional<double>(s.gm6.limits[1]):std::nullopt,s.gm6.connected?std::optional<double>(s.gm6.limits[2]):std::nullopt};
    if(wujie){if(!s.valid||!s.power.acceptedValid)return {};
        return {s.power.accepted.sustained,s.power.accepted.shortBoost,s.power.accepted.peak};}
    return {s.apu.stapmLimitWatts,s.apu.slowLimitWatts,s.apu.fastLimitWatts};
}
std::string wujiePowerComparisonText(const SettingsSnapshot& s){
    const auto value=[](const auto& v){return v?std::format("{:.0f}",double(*v)):std::string("—");};
    return std::string("Лимиты: EC 46A/46B/46F · сверка SMU Fast/Slow/STAPM: ")+
        value(s.apu.fastLimitWatts)+"/"+value(s.apu.slowLimitWatts)+"/"+value(s.apu.stapmLimitWatts)+" Вт"+
        (s.apu.stale?" · SMU: последнее показание":"")+
        (s.valid&&s.power.acceptedValid?"":" · EC: нет свежего результата");
}
void projectSettingsHidZone(SettingsSnapshot& snapshot,LightingZone zone){
    snapshot.keyboardSupported=snapshot.keyboardWritable=false;
    if(const auto info=snapshot.lighting.find(zone)){
        snapshot.keyboardSupported=true;snapshot.keyboardWritable=info->writable;snapshot.keyboardOwnerStatus=info->status;
        if(info->configured){const auto& c=*info->configured;
            snapshot.keyboard=static_cast<std::uint8_t>((c.level<<5)|(c.level?0:2));
            snapshot.effect=c.effect==5?0x80:0;snapshot.breathing=c.effect==2?1:0;
            for(int i=0;i<3;++i)snapshot.color[i]=static_cast<std::uint8_t>((int(c.rgb[i])*50+127)/255);
        }
    }
}
void collectSettingsHidLighting(SettingsSnapshot& snapshot,windows::IteLightingController* controller,LightingZone zone){
    if(!controller)return;
    snapshot.lighting=readLightingZones(*controller);
    projectSettingsHidZone(snapshot,zone);
}
void projectSettingsSingleZone(SettingsSnapshot& snapshot,const KeyboardBacklightState& k,bool ownerWritable,std::string_view ownerStatus){
    snapshot.keyboardSupported=k.supported;snapshot.keyboard=k.keyboard;snapshot.effect=k.effect;
    snapshot.breathing=k.breathing;snapshot.color=k.color;snapshot.keyboardPulseRaw=k.pulseRaw;
    snapshot.keyboardRetained=k.retainedForDisplay;
    if(k.retainedForDisplay)snapshot.keyboardIdle.reset();
    snapshot.keyboardWritable=k.supported&&!k.retainedForDisplay&&ownerWritable;snapshot.keyboardOwnerStatus=ownerStatus;
}
void collectSettingsSingleZone(SettingsSnapshot& snapshot,const KeyboardBacklightState& k){
    const auto owner=windows::inspectLightingOwnership();
    projectSettingsSingleZone(snapshot,k,owner.writable,owner.status);
}
bool observedGmPowerMatches(const SettingsSnapshot& snapshot,const std::array<int,3>& expected,std::string_view profile){
    return snapshot.gm6.connected && !(expected!=std::array<int,3>{}&&snapshot.gm6.pl4Double&&!gmPowerDoubleCodec(profile)) && snapshot.gm6.limits==expected;
}
bool recoverSettingsObservation(SettingsSnapshot& snapshot,const SettingsSnapshotContext& context,
                                windows::NativeBatteryController* battery,MonitoringReader& reader,
                                windows::ApuPowerReader& power){
    try{snapshot=collectSettingsSnapshot(context);if(battery)snapshot.nativeBattery=battery->read();
        snapshot.batteryTelemetry=reader.readBatteryOnly();if(context.laptop)snapshot.apu=power.read(true);return true;}
    catch(...){snapshot={};return false;}
}
std::optional<KeyboardIdleState> collectSettingsKeyboardIdle(const SettingsSnapshotContext& c){
    if(!c.ec||c.laptop||c.gm||c.hid)return {};
    return readKeyboardIdle(*c.ec);
}
SettingsSnapshot collectSettingsSnapshot(const SettingsSnapshotContext& c) {
    const auto collectHid=[&](SettingsSnapshot& next){if(c.hidObservationOverride)c.hidObservationOverride(next);else collectSettingsHidLighting(next,c.hid,c.zone);};
    if(!c.ec&&!c.laptop&&!c.gm){SettingsSnapshot next{};next.onBattery=windows::apuOnBattery();collectHid(next);next.valid=true;return next;}
    if(c.gm){SettingsSnapshot next{};next.gm6=c.gm->read();next.onBattery=windows::apuOnBattery();next.valid=true;if(next.gm6.keyboard)collectSettingsSingleZone(next,*next.gm6.keyboard);collectHid(next);if(c.cycle)next.cycle=c.cycle->status();return next;}
    if(c.laptop){SettingsSnapshot next{};next.oem=c.laptop->read();next.onBattery=windows::apuOnBattery();
        if(next.oem.chargeEnd)next.battery=static_cast<std::uint8_t>(*next.oem.chargeEnd==100?0:*next.oem.chargeEnd);
        next.valid=true;return next;}
    const EcAccess::Transaction transaction(*c.ec);
    SettingsSnapshot next{};
    next.onBattery=windows::apuOnBattery();
    next.battery=c.ec->readStable(ec::kBatteryChargeControl);
    next.batteryGate=c.ec->readStable(0x742);
    next.batteryPlatform=c.ec->readStable(0x7C3);
    next.batteryProject=c.ec->readStable(0x740);
    next.batteryRom=c.ec->readStable(0x770);
    next.power=readEcPerformance(*c.ec);
    next.performance=next.power.rawMode;
    LightingControlContext lighting;lighting.ec=c.ec;lighting.gm=c.gm;
    collectSettingsSingleZone(next,readLighting(lighting));if(c.cycle)next.cycle=c.cycle->status();
    try{next.keyboardIdle=collectSettingsKeyboardIdle(c);}catch(...){next.keyboardIdle.reset();}
    next.adapterClass=c.ec->readStable(ec::kAdapterClass);
    next.powerInput=c.ec->readStable(ec::kPowerInput);
    next.typecStatus=c.ec->readStable(ec::kTypeCStatus);
    // No periodic I2EC priority probe for removed controls; CLI status is explicit.
    next.typecPowerA=c.ec->readStable(ec::kTypeCPowerA);
    next.typecPowerB=c.ec->readStable(ec::kTypeCPowerB);
    next.chargerRead=true;
    next.valid=true;
    next.performance&=ec::kPerformancePresetMask;
    next.keyboard&=ec::kKeyboardMask;
    next.effect&=ec::kKeyboardRainbowBit;
    return next;
}
void collectSettingsSupplemental(SettingsSnapshot& pending,const SettingsSupplementalContext& c){
    const auto& previous=c.previous;
    const bool gm6NativePower=c.gm6NativePower,quickBattery=c.quickBattery,quickProfile=c.quickProfile,
        saveChoice=c.saveChoice,intelCpuUi=c.intelCpuUi,gm5Ui=c.gm5Ui;
    const int command=c.command,voltageMode=c.batteryVoltageMode;
    const auto started=c.started,appliedAt=c.appliedAt;
    auto* ec=c.ec;auto* oemControl=c.laptop;auto* gm6Controller=c.gm;
    auto* nativeBattery=c.nativeBattery;auto* voltageBattery=c.voltageBattery;
    auto& batteryReader=c.batteryReader;auto& intelMonitor=c.intelMonitor;auto& apuPowerReader=c.apuPowerReader;
    const auto& log=c.log;auto& lastApuLog=c.lastApuLog;
    if(gm6NativePower){
        // Retain original sample timestamps; never stamp old values fresh.
        pending.nativeBattery=previous.nativeBattery;pending.batteryTelemetry=previous.batteryTelemetry;
        pending.osBattery=previous.osBattery;pending.ecBattery=previous.ecBattery;pending.batteryPolicy=previous.batteryPolicy;
        pending.intelWatts=previous.intelWatts;pending.intelPowerSource=previous.intelPowerSource;pending.intelPowerStatus=previous.intelPowerStatus;
    }
    const auto monitoringAllowed=[&]{return !quickBattery&&!quickProfile&&!(gm6NativePower&&!saveChoice&&c.priorityPending());};
    pending.voltageBattery=previous.voltageBattery;
    if(!monitoringAllowed()){pending.voltageBattery.voltageRetained=true;pending.voltageBattery.voltageOnline=false;}
    if(nativeBattery&&monitoringAllowed())pending.nativeBattery=nativeBattery->read();
    if(voltageBattery&&monitoringAllowed())pending.voltageBattery=voltageBattery==nativeBattery?pending.nativeBattery:voltageBattery->read();
    if(quickBattery){
        // Confirm only this battery operation now; unrelated telemetry
        // retains its original timestamps and updates on normal poll.
        pending.voltageBattery=voltageBattery->read();
        if(voltageBattery==nativeBattery)pending.nativeBattery=pending.voltageBattery;
        if(pending.voltageBattery.projectRaw)pending.batteryProject=*pending.voltageBattery.projectRaw;
        if(pending.voltageBattery.platformRaw)pending.batteryPlatform=*pending.voltageBattery.platformRaw;
        if(pending.voltageBattery.romIdRaw)pending.batteryRom=*pending.voltageBattery.romIdRaw;
        pending.keyboardRetained=pending.keyboardSupported;
        pending.batteryTelemetry.dynamicStale=true;pending.osBattery.dynamicStale=true;
        pending.batteryTelemetry.healthStale=pending.batteryTelemetry.healthPercent.has_value();
        pending.osBattery.healthStale=pending.osBattery.healthPercent.has_value();
        pending.ecBattery.retainedForDisplay=true;
        // Preserve APU's own sampled time/freshness and edit
        // availability, as in the existing quick power path.
        logMessage("battery",std::format("software_command_timing command={} apply_ms={} confirmation_ms={} monitoring=deferred next=normal_poll physical_response_measured=false",
            command,appliedAt-started,GetTickCount64()-appliedAt));
    }
    const bool lenovoEnergy=oemControl&&oemControl->chargePolicyId()=="lenovo-legion-kwcn-energy";
    if(saveChoice&&voltageMode>=0&&voltageMode<3&&lenovoEnergy&&(!pending.oem.chargeModeWritable||pending.oem.chargeMode!=voltageMode))
        throw std::runtime_error("Lenovo: повторное чтение не подтвердило режим зарядки");
    if(saveChoice&&voltageMode>=0&&voltageMode<3&&!lenovoEnergy&&
        (!pending.voltageBattery.connected||!pending.voltageBattery.writable||
         !pending.voltageBattery.mode||static_cast<int>(*pending.voltageBattery.mode)!=voltageMode||
         (voltageMode!=0&&!pending.voltageBattery.appPresent)))
        throw std::runtime_error("Повторное чтение EC не подтвердило штатный профиль напряжения");
    if(monitoringAllowed()){
        BatteryReading osObservation;
        pending.batteryTelemetry=batteryReader.readBatteryOnly(&osObservation);
        if(monitoringAllowed())pending.osBattery=std::move(osObservation);
    }
    if(monitoringAllowed())pending.ecBattery=batteryReader.readEcBatteryOnly();
    if(monitoringAllowed())pending.batteryPolicy=batteryReader.readBatteryPowerPolicy();
    if(intelCpuUi&&monitoringAllowed()){MonitoringSnapshot sample;intelMonitor.read(sample);pending.intelWatts=sample.processors[0].powerWatts;pending.intelPowerSource=sample.processors[0].powerSource;pending.intelPowerStatus=sample.processorStatus;}
    if(quickProfile)logMessage("gm6ir0c",std::format("software_command_timing command={} apply_ms={} profile_refresh_ms={} monitoring=deferred next=normal_poll",command,appliedAt-started,GetTickCount64()-appliedAt));
    // A separate, read-only SETs diagnostic after releasing the EC transaction.
    if(ec&&monitoringAllowed())pending.apu = apuPowerReader.read(ec->supports(DeviceFeature::SmuTelemetry));
    else if(oemControl&&monitoringAllowed()){pending.apu=apuPowerReader.read(true);pending.apu.status+=" · "+pending.oem.status;}
    else if(c.profile=="minisforum-hpbsd"&&monitoringAllowed())pending.apu=apuPowerReader.read(true);
    auto apuLog = gm6Controller ? std::format("SETs {} [{}]: package_power={}W; configured limits are EC {}, not measured power ceilings",gm5Ui?"AMD":"Intel",c.profile,gm5Ui?pending.apu.fastPowerWatts.value_or(-1):pending.intelWatts.value_or(-1),gm5Ui?"SPL/SPPT/FPPT":"PL1/PL2/PL4") : std::format("SETs APU: PM=0x{:X} fast_power={}W Fast={}W Slow={}W STAPM={}W stale={} status={}",
        pending.apu.tableVersion, pending.apu.fastPowerWatts.value_or(-1),
        pending.apu.fastLimitWatts.value_or(-1), pending.apu.slowLimitWatts.value_or(-1),
        pending.apu.stapmLimitWatts.value_or(-1), pending.apu.stale, pending.apu.status);
    auto apuSignature=gm6Controller?std::string(c.profile):
        std::format("{}|{}|{}|{}|{}|{}",pending.apu.tableVersion,
            pending.apu.fastLimitWatts.value_or(-1),pending.apu.slowLimitWatts.value_or(-1),
            pending.apu.stapmLimitWatts.value_or(-1),pending.apu.stale,pending.apu.status);
    if(c.profile=="wu-jie-15-xpro"){const auto accepted=audit::ecPower(pending.power);apuLog+=" ec_power="+accepted;apuSignature+='|'+accepted;}
    if (monitoringAllowed() && log && (ec||oemControl||gm6Controller) && (saveChoice||apuSignature != lastApuLog)) { lastApuLog = apuSignature; log(apuLog); }

}
}
