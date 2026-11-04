#pragma once
#include "Monitoring.h"
#include "LaptopControl.h"
#include "KeyboardBacklight.h"
#include <map>
#include <algorithm>
#include <type_traits>
namespace mc {
template<class T>bool retainDisplayValue(std::optional<T>& fresh,const std::optional<T>& last){
    if constexpr(std::is_floating_point_v<T>)if(fresh&&!std::isfinite(*fresh))fresh.reset();
    if(!fresh&&last){fresh=last;return true;}return false; // Zero/false are successful observations.
}
// One OS/model-independent presentation cache. A failed query is not a new zero.
// Keep original timestamps, source and identity; never retain capability/ownership
// permissions or feed this cache to a cooling loop, setter or diagnostic collector.
class MonitoringDisplayCache {
    MonitoringSnapshot last_;std::map<std::string,std::uint64_t> at_;
public:
    MonitoringSnapshot update(MonitoringSnapshot current){
        current.retainedForDisplay.clear();const auto now=batteryMonotonicMs();
        // Default graphs are read-only observations, never control state.
        // A skipped/failed read is not a confirmed exit from Default. Bridge
        // short collector gaps without renewing timestamps or copying permits.
        // Explicit Inactive (Manual/Boost/unknown table/identity) clears now.
        if(!current.nativeDefaultFans&&current.nativeDefaultFanObservation==NativeDefaultFanObservation::Unavailable&&
           last_.nativeDefaultFans&&nativeDefaultFanFresh(last_.nativeDefaultFans->channels[0])&&
           nativeDefaultFanFresh(last_.nativeDefaultFans->channels[1])&&
           current.processorModels==last_.processorModels){
            current.nativeDefaultFans=last_.nativeDefaultFans;
            for(auto& channel:current.nativeDefaultFans->channels)channel.retained=true;
        }
        const auto keep=[&](const std::string& key,auto& value,const auto& old,std::string* source=nullptr,const std::string* oldSource=nullptr,std::uint64_t maximumAge=0,std::uint64_t sampledAt=0){
            using Value=typename std::remove_reference_t<decltype(value)>::value_type;
            if constexpr(std::is_floating_point_v<Value>)if(value&&!std::isfinite(*value))value.reset();
            if(maximumAge&&!value){const auto at=at_[key];if(!at||now<at||now-at>maximumAge)return false;}
            if(!retainDisplayValue(value,old)){if(value)at_[key]=sampledAt?sampledAt:now;return false;}
            if(source&&oldSource)*source=*oldSource;
            current.retainedForDisplay.push_back({key,at_[key]});return true;
        };
        for(unsigned i=0;i<2;++i){
            auto& p=current.processors[i];const auto& old=last_.processors[i];const auto key="processor."+std::to_string(i)+".";
            if(!current.processorModels[i].empty()&&!last_.processorModels[i].empty()&&current.processorModels[i]!=last_.processorModels[i])continue;
            if(current.processorModels[i].empty())current.processorModels[i]=last_.processorModels[i];
            keep(key+"temperature",p.celsius,old.celsius,&p.temperatureSource,&old.temperatureSource);
            keep(key+"load",p.usagePercent,old.usagePercent,&p.usageSource,&old.usageSource);
            keep(key+"clock",p.clockMhz,old.clockMhz,&p.clockSource,&old.clockSource);
            keep(key+"power",p.powerWatts,old.powerWatts,&p.powerSource,&old.powerSource);
            keep(key+"rpm",p.fanRpm,old.fanRpm,&p.fanSource,&old.fanSource,10000,current.processorSampledAtMs);
            keep(key+"pwm",p.fanPwm,old.fanPwm,nullptr,nullptr,10000,current.processorSampledAtMs);
            for(unsigned disk=0;disk<2;++disk){auto& d=disk?current.disks[i]:current.memory[i];const auto& prev=disk?last_.disks[i]:last_.memory[i];
                if(d.present==false||(!d.model.empty()&&!prev.model.empty()&&d.model!=prev.model)||(!d.serial.empty()&&!prev.serial.empty()&&d.serial!=prev.serial))continue;
                if(keep(std::string(disk?"disk.":"memory.")+std::to_string(i),d.celsius,prev.celsius,&d.source,&prev.source)){if(d.model.empty())d.model=prev.model;if(d.serial.empty())d.serial=prev.serial;d.status+=" · последнее подтверждённое показание";}
            }
        }
        if(current.battery.deviceId.empty()&&current.battery.present!=false)current.battery.deviceId=last_.battery.deviceId;
        retainBatteryReadings(current.battery,last_.battery);
        keep("cpu.cores_power",current.cpuCorePowerWatts,last_.cpuCorePowerWatts,&current.cpuCorePowerSource,&last_.cpuCorePowerSource);
        keep("pch.temperature",current.pchCelsius,last_.pchCelsius,&current.pchSource,&last_.pchSource);
        keep("soc.temperature",current.socCelsius,last_.socCelsius,&current.socSource,&last_.socSource);
        keep("apu.power",current.apuPackagePowerWatts,last_.apuPackagePowerWatts,&current.apuPackagePowerSource,&last_.apuPackagePowerSource);
        keep("npu.load",current.npuUsagePercent,last_.npuUsagePercent,&current.npuUsageSource,&last_.npuUsageSource);
        keep("npu.clock",current.npuClockMhz,last_.npuClockMhz,&current.npuClockSource,&last_.npuClockSource);
        keep("npu.power",current.npuPowerWatts,last_.npuPowerWatts,&current.npuPowerSource,&last_.npuPowerSource);
        for(auto& card:current.gpuCards.cards){const auto old=std::find_if(last_.gpuCards.cards.begin(),last_.gpuCards.cards.end(),[&](const auto& v){return v.adapterId==card.adapterId&&v.physicalId==card.physicalId;});
            const auto key="gpu."+card.adapterId+".";
            if(old==last_.gpuCards.cards.end()){for(const auto* field:{"load","temperature","clock","power"})at_[key+field]=now;continue;}
            card.usageStale=keep(key+"load",card.usagePercent,old->usagePercent,&card.usageSource,&old->usageSource);
            keep(key+"temperature",card.celsius,old->celsius,&card.temperatureSource,&old->temperatureSource);
            keep(key+"clock",card.clockMhz,old->clockMhz,&card.clockSource,&old->clockSource);
            keep(key+"power",card.powerWatts,old->powerWatts,&card.powerSource,&old->powerSource);
        }
        if(!current.retainedForDisplay.empty())current.processorStatus+=" · отображаются последние подтверждённые данные";
        if(const auto* selected=gpuLoadCard(current.gpuCards,current.gpuCards.selectionReason))current.gpuCards.selectedAdapterId=selected->adapterId;
        last_=current;return current;
    }
};
class GpuDisplayCache {
    std::vector<GpuReading> last_;std::map<std::string,std::uint64_t> at_;
public:
    std::vector<GpuReading> update(std::vector<GpuReading> rows){
        if(rows.empty()||std::any_of(rows.begin(),rows.end(),[](const auto& r){return r.deviceAbsent;})){last_=rows;at_.clear();return rows;}
        if(rows.size()==1&&!rows[0].available&&rows[0].uuid.empty()&&!last_.empty()){
            const auto error=rows[0].status;rows=last_;
            for(auto& r:rows){r.available=false;r.powerControl=false;r.retainedForDisplay=true;r.status=error+" · последние подтверждённые показания";r.retainedFieldTimes.clear();for(const auto& [field,when]:at_)if(field.starts_with(r.uuid+"/"))r.retainedFieldTimes.emplace_back(field,when);}
        }else for(auto& r:rows){const auto prev=std::find_if(last_.begin(),last_.end(),[&](const auto& v){return !r.uuid.empty()&&r.uuid==v.uuid&&r.physicalId==v.physicalId;});
            const GpuReading empty;const auto& old=prev==last_.end()?empty:*prev;
            const auto keep=[&](const char* field,auto& value,const auto& before){const auto key=r.uuid+"/"+field;if(retainDisplayValue(value,before)){r.retainedForDisplay=true;r.retainedFieldTimes.emplace_back(key,at_[key]);}else if(value)at_[key]=batteryMonotonicMs();};
            const bool missingTemperature=!r.temperature;keep("temperature",r.temperature,old.temperature);if(missingTemperature&&old.temperature)r.temperatureSource=old.temperatureSource;
            keep("load",r.utilization,old.utilization);keep("memory_load",r.memoryUtilization,old.memoryUtilization);keep("clock",r.graphicsMhz,old.graphicsMhz);keep("memory_clock",r.memoryMhz,old.memoryMhz);keep("pstate",r.pstate,old.pstate);
            const bool missingPower=!r.powerMw;keep("power",r.powerMw,old.powerMw);
            if(missingPower&&old.powerMw){r.powerSource=old.powerSource;if(r.powerSource.find("последнее показание")==std::string::npos)r.powerSource+=" · последнее показание";}
            keep("hotspot",r.hotspotCelsius,old.hotspotCelsius);keep("memory_temperature",r.memoryCelsius,old.memoryCelsius);keep("memory_junction",r.memoryJunctionCelsius,old.memoryJunctionCelsius);
            keep("vram_total",r.totalBytes,old.totalBytes);keep("vram_used",r.usedBytes,old.usedBytes);keep("rpm",r.fanRpm,old.fanRpm);
            if(prev==last_.end()||r.powerUnit==old.powerUnit){
                keep("power_setting",r.powerValue,old.powerValue);keep("power_minimum",r.powerMinimum,old.powerMinimum);keep("power_maximum",r.powerMaximum,old.powerMaximum);keep("power_default",r.powerDefault,old.powerDefault);keep("power_step",r.powerStep,old.powerStep);
                keep("limit_mw",r.limitMw,old.limitMw);keep("enforced_mw",r.enforcedMw,old.enforcedMw);keep("default_mw",r.defaultMw,old.defaultMw);keep("minimum_mw",r.minimumMw,old.minimumMw);keep("maximum_mw",r.maximumMw,old.maximumMw);
            }
            if(r.retainedForDisplay)r.status+=" · последние подтверждённые показания";
        }
        last_=rows;return rows;
    }
};
class EcBatteryDisplayCache {
    EcBatteryReading last_;
public:
    EcBatteryReading update(EcBatteryReading fresh){
        if(fresh.battery.present==false){last_={};return fresh;}
        if(fresh.coherent){
            if(fresh.retainedForDisplay||fresh.battery.dynamicStale)fresh.aging.authoritative=false;
            if(last_.coherent&&fresh.profileId==last_.profileId&&!batteryIdentityChanged(fresh.battery,last_.battery))
                fresh.aging=retainBatteryAgingDisplay(std::move(fresh.aging),last_.aging,batteryMonotonicMs());
            // Manufacture date is immutable metadata, not a live EC sensor.
            // A paused/partial sample must not erase a verified date; explicit
            // battery removal above or a different identity still clears it.
            if(fresh.battery.manufactured.empty()&&last_.coherent&&fresh.profileId==last_.profileId&&
               !batteryIdentityChanged(fresh.battery,last_.battery))fresh.battery.manufactured=last_.battery.manufactured;
            if(!fresh.sampledAtMs)fresh.sampledAtMs=fresh.battery.dynamicAtMs[0]?fresh.battery.dynamicAtMs[0]:batteryMonotonicMs();
            last_=fresh;return fresh;}
        if(last_.coherent&&fresh.supported&&(fresh.profileId.empty()||last_.profileId==fresh.profileId)){
            auto shown=last_;shown.coherent=false;shown.retainedForDisplay=true;shown.battery.dynamicStale=true;
            shown.aging=retainBatteryAgingDisplay({},last_.aging,batteryMonotonicMs());
            shown.status=fresh.status+" · последние подтверждённые данные EC";return shown;
        }
        return fresh;
    }
};
class LaptopDisplayCache {
    LaptopControlSnapshot last_;bool initialized_{};
public:
    LaptopControlSnapshot update(LaptopControlSnapshot fresh){
        if(!initialized_){if(fresh.connected){last_=fresh;initialized_=true;}return fresh;}
        const auto keep=[&](auto& value,const auto& old){if(retainDisplayValue(value,old))fresh.retainedForDisplay=true;};
        keep(fresh.chargeStart,last_.chargeStart);keep(fresh.chargeEnd,last_.chargeEnd);keep(fresh.fnLock,last_.fnLock);keep(fresh.nativeTurbo,last_.nativeTurbo);
        keep(fresh.stockCpuProfile,last_.stockCpuProfile);
        keep(fresh.keyboardLevel,last_.keyboardLevel);keep(fresh.keyboardTimeoutSeconds,last_.keyboardTimeoutSeconds);keep(fresh.keyboardActualDuty,last_.keyboardActualDuty);keep(fresh.keyboardPwmPeriod,last_.keyboardPwmPeriod);
        keep(fresh.fanRpm[0],last_.fanRpm[0]);keep(fresh.fanRpm[1],last_.fanRpm[1]);keep(fresh.cpuCelsius,last_.cpuCelsius);keep(fresh.batteryCelsius,last_.batteryCelsius);
        if(fresh.retainedForDisplay)fresh.status+=" · последние подтверждённые данные";
        last_=fresh;return fresh; // connected/writable remain RAW; no permission copied.
    }
};
inline int retainedControlDisplayValue(int fresh,int previous)noexcept{return fresh<0?previous:fresh;}
class KeyboardDisplayCache {
    std::optional<KeyboardBacklightState> last_;
public:
    std::optional<KeyboardBacklightState> update(std::optional<KeyboardBacklightState> fresh){
        if(fresh&&fresh->supported){last_=fresh;return fresh;}
        if(!last_)return {};
        auto shown=*last_;shown.retainedForDisplay=true;return shown;
    }
};
}
