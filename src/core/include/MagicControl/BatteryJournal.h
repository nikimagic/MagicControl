#pragma once
#include "Monitoring.h"
#include "Logging.h"
#include <cmath>
#include <format>
#include <map>
#include <mutex>

namespace mc {
// One process-wide journal shared by FANs/SETs readers. No polling or setters.
inline void journalBattery(const BatteryReading& b,std::string_view stream,std::string_view context={}){
    // Key SOC/flow/driver events remain in the general journal with AllLogs=1.
    if(!anyLoggingEnabled())return;
    struct Last{int percent{-1};std::string flags;};
    static std::mutex mutex;static std::map<std::string,Last> previous;
    const std::string key=std::string(stream)+"/"+b.deviceId;
    const auto metric=[](std::optional<double> v){return v?std::format("{:.3f}",*v):std::string("unknown");};
    const auto flow=b.flow==BatteryFlow::Charging?"charging":b.flow==BatteryFlow::Discharging?"discharging":b.flow==BatteryFlow::Idle?"idle":"unknown";
    const auto raw=[](std::optional<std::uint32_t> v){return v?std::format("0x{:08X}",*v):std::string("unknown");};
    const auto flags=std::format("present={} flow={} ac={} driver_critical={} driver_state={} capabilities={} technology={} alert1={} alert2={} critical_bias={} status=[{}] {}",b.present?(*b.present?"true":"false"):"unknown",flow,b.onlineKnown?(b.online?"online":"offline"):"unknown",b.powerState?((*b.powerState&8)?"true":"false"):"unknown",raw(b.powerState),raw(b.capabilities),raw(b.technology),raw(b.alert1),raw(b.alert2),raw(b.criticalBias),b.powerStatus,context);
    int percent=-1;if(b.present!=false&&b.percent&&std::isfinite(*b.percent)&&*b.percent>=0&&*b.percent<=100)percent=static_cast<int>(std::lround(*b.percent));
    int old=-1;bool percentChanged=false,flagsChanged=false;std::string oldFlags;
    {
        std::lock_guard lock(mutex);
        auto& last=previous[key];old=last.percent;oldFlags=last.flags;
        percentChanged=percent>=0&&percent!=old;
        flagsChanged=flags!=last.flags;
        if(percent>=0)last.percent=percent;
        last.flags=flags;
    }
    const auto details=std::format("stream={} device=[{}] percent={} W={} V={} mA={} C={} cycles={} source=[{}] design_capacity={} full_capacity={} remaining_capacity={} capacity_unit=[{}] capacity_relative={} health_percent={} discharge_seconds={} charge_seconds={} {}",
        stream,b.deviceId,percent,metric(b.watts),metric(b.volts),metric(b.milliamps),metric(b.celsius),
        b.cycles?std::to_string(*b.cycles):"unknown",b.source,metric(b.designCapacity),metric(b.fullCapacity),metric(b.remainingCapacity),b.capacityUnit,b.capacityRelative,metric(b.healthPercent),b.estimatedDischargeSeconds?std::to_string(*b.estimatedDischargeSeconds):"unknown",b.estimatedChargeSeconds?std::to_string(*b.estimatedChargeSeconds):"unknown",flags)+std::format(" dynamic_stale={} percent_at_ms={} power_at_ms={} temperature_at_ms={} health_stale={} health_sampled_at_monotonic_ms={} health_source=[{}]",b.dynamicStale,b.dynamicAtMs[0],b.dynamicAtMs[1],b.dynamicAtMs[2],b.healthStale,b.healthAtMs,b.healthSource);
    if(percentChanged)logMessage("battery",std::format("SOC {} previous={} current={} delta={} skipped_percent_samples={} {}",
        old<0?"initial":"changed",old,percent,old<0?0:percent-old,old<0?0:std::max(0,std::abs(percent-old)-1),details));
    if(flagsChanged)logMessage("battery",std::format("FLAGS {} previous=[{}] {}",oldFlags.empty()?"initial":"changed",oldFlags,details));
}
inline void journalBatteryRegisters(const EcBatteryReading& reading,std::uint64_t now=batteryMonotonicMs()){
    if(!batteryLoggingEnabled()||!reading.coherent||reading.registers.empty())return;
    struct Last {std::map<std::uint16_t,EcBatteryRegisterReading> fields;int percent{-1};std::uint64_t snapshotAt{},diagnosticSequence{};};
    static std::mutex mutex;static std::map<std::string,Last> states;
    const auto& b=reading.battery;
    const int percent=b.percent?static_cast<int>(std::lround(*b.percent)):-1;
    const auto metric=[](std::optional<double> v){return v?std::format("{:.3f}",*v):std::string("unknown");};
    const auto capacity=[](std::optional<std::uint16_t> v){return v?std::to_string(*v):std::string("unknown");};
    const auto hex=[](std::optional<std::uint16_t> v,int width){return v?std::format("0x{:0{}X}",*v,width*2):std::string("unknown");};
    auto context=std::format("device=[{}] percent={} W={} V={} mA={} C={} design_mAh={} full_mAh={} remaining_mAh={} core_stable=true atomic=false",
        b.deviceId,percent,metric(b.watts),metric(b.volts),metric(b.milliamps),metric(b.celsius),capacity(reading.designMah),capacity(reading.fullMah),capacity(reading.remainingMah));
    if(reading.profileId=="lenovo-legion-kwcn"){
        const auto ecFlow=b.flow==BatteryFlow::Charging?"charging":b.flow==BatteryFlow::Discharging?"discharging":b.flow==BatteryFlow::Idle?"idle":"unknown";
        const auto direction=!b.milliamps?"unknown":*b.milliamps<0?"negative":*b.milliamps>0?"positive":"zero";
        context+=std::format(" ec_status={} current_direction={} current_source=0xC4D0",ecFlow,direction);
    }
    const bool rawCapacity=reading.profileId=="mechrevo-gm6ir0c"||reading.profileId=="wu-jie-15-xpro";
    if(rawCapacity){
        const auto raw=[&](unsigned address)->std::optional<std::uint16_t>{
            for(const auto& field:reading.registers){if(field.address==address)return field.value;}
            return {};
        };
        context+=std::format(" capacity_unit_raw={} design_capacity_raw={} full_capacity_raw={} remaining_capacity_raw={} raw_counts_not_converted=true",
            capacity(raw(0x400)),capacity(raw(0x402)),capacity(raw(0x404)),capacity(raw(0x436)));
    }
    std::string changes,snapshot,diagnostic;std::string reason;
    {
        std::lock_guard lock(mutex);auto& last=states[b.deviceId];
        if(reading.diagnostics&&reading.diagnostics->sequence!=last.diagnosticSequence){
            const auto& d=*reading.diagnostics;last.diagnosticSequence=d.sequence;
            diagnostic=std::format("CURRENT_CALC kind={} sample_seq={} reason={} core_at_ms={} started_ms={} finished_ms={} duration_ms={} atomic=false coherence=per_field_double_read physical_current_verified=false {} fields=[",
                d.kind,d.sequence,d.reason,d.coreAtMs,d.startedAtMs,d.finishedAtMs,d.finishedAtMs-d.startedAtMs,context);
            bool separator=false;
            for(const auto& field:d.fields){
                if(separator)diagnostic+=';';separator=true;
                diagnostic+=std::format("addr=0x{:03X} name={} width={} endian={} raw={} unsigned={} status=[{}]",
                    field.address,field.name,field.width,field.bigEndian?"BE":"LE",hex(field.value,field.width),capacity(field.value),field.status);
                if(field.signedValue){
                    const auto value=field.value?std::to_string(*field.value>=0x8000?int(*field.value)-65536:int(*field.value)):"unknown";
                    diagnostic+=" int16_signed="+value+" signed_encoding=twos_complement_raw";
                }
            }
            diagnostic+=']';
            // Relate public state/rate to the internal evidence without forcing
            // the individual core fields to come from the later auxiliary read.
            for(const auto& field:reading.registers)
                if(field.address==0x400||field.address==0x432||field.address==0x434||field.address==0x436||field.address==0x404||field.address==0x438||field.address==0x490||field.address==0x497)
                    diagnostic+=std::format(" core_0x{:03X}={}",field.address,hex(field.value,field.width));
        }
        const bool initial=last.fields.empty();
        const bool snapshotDue=initial||percent!=last.percent||now-last.snapshotAt>=10000;
        for(const auto& field:reading.registers){
            // Keep controls/status changes immediate. Current, voltage, temperature
            // and other noisy measurements share the existing 10s/SOC cadence.
            const auto old=last.fields.find(field.address);
            const bool availabilityChanged=old!=last.fields.end()&&
                (old->second.value.has_value()!=field.value.has_value()||old->second.status!=field.status);
            if(field.sampledTelemetry&&!snapshotDue&&!availabilityChanged)continue;
            if(old!=last.fields.end()&&(availabilityChanged||(!field.sampledTelemetry&&old->second.value!=field.value))){
                if(!changes.empty())changes+=';';
                changes+=std::format("addr=0x{:03X} name={} previous={} current={} status=[{}]",field.address,field.name,hex(old->second.value,field.width),hex(field.value,field.width),field.status);
                if(rawCapacity&&(field.address==0x400||field.address==0x402||field.address==0x404||field.address==0x436))
                    changes+=std::format(" previous_decimal={} current_decimal={}",capacity(old->second.value),capacity(field.value));
                // An unavailable reading is NOT zero and cannot clear bits.
                if(old->second.value&&field.value){
                    const auto previous=*old->second.value,current=*field.value;
                    changes+=std::format(" xor=0x{:04X} set=0x{:04X} cleared=0x{:04X}",previous^current,(~previous)&current,previous&(~current));
                }
            }
            last.fields[field.address]=field;
        }
        if(snapshotDue){
            reason=initial?"initial":percent!=last.percent?"soc_change":"periodic_10s";
            for(const auto& field:reading.registers){
                if(!snapshot.empty())snapshot+=';';
                if(initial)snapshot+=std::format("addr=0x{:03X} name={} width={} endian={} meaning={} value={} decimal={} status=[{}]",
                    field.address,field.name,field.width,field.bigEndian?"BE":"LE",field.confirmed?"documented_field":"candidate_unverified",hex(field.value,field.width),field.value?std::to_string(*field.value):"unknown",field.status);
                else{
                    snapshot+=std::format("0x{:03X}={}/{}",field.address,hex(field.value,field.width),field.value?std::to_string(*field.value):"unknown");
                    if(field.status!="ok")snapshot+=" status=["+field.status+"]";
                }
                if(reading.profileId=="lenovo-legion-kwcn"&&field.signedValue){
                    const auto signedRaw=field.value?std::to_string(*field.value>=0x8000?int(*field.value)-65536:int(*field.value)):"unknown";
                    snapshot+=" int16_signed="+signedRaw;
                }
            }
            last.snapshotAt=now;
        }
        last.percent=percent;
    }
    if(!changes.empty())logMessage("battery_candidates","CHANGED "+context+" fields=["+changes+"]");
    if(!snapshot.empty())logMessage("battery_candidates","SNAPSHOT reason="+reason+" "+context+" fields=["+snapshot+"]");
    if(!diagnostic.empty())logMessage("battery_candidates",diagnostic);
}
}
