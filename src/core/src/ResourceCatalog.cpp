#include "MagicControl/ResourceCatalog.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include <cmath>
#include <cstdlib>

namespace mc {
namespace {
void rejectNonFinite(ResourceMeasurement& measurement){
    const bool invalidValue=measurement.value&&!std::isfinite(*measurement.value);
    const bool invalidRaw=measurement.raw&&!std::isfinite(*measurement.raw);
    if(!invalidValue&&!invalidRaw)return;
    if(invalidValue)measurement.value.reset();
    if(invalidRaw)measurement.raw.reset();
    if(!measurement.status.empty())measurement.status+="; ";
    measurement.status+=invalidValue?"non-finite measurement rejected":"non-finite raw value rejected";
}
}
std::vector<ResourceObservation> observeSensorResources(std::span<const SensorReading> readings){
    std::vector<ResourceObservation> out;out.reserve(readings.size());
    for(std::size_t i=0;i<readings.size();++i){const auto& source=readings[i];
        ResourceObservation sensor;sensor.kind=ResourceKind::Sensor;sensor.name=source.device;
        sensor.presentationSlot=static_cast<unsigned>(i);
        sensor.identity.scope="diagnostic sample index; not a durable identity";
        sensor.measurements.push_back({source.metric,source.unit,source.source,source.status,
            source.value,source.sampledAtMs,std::nullopt,source.raw});
        if(source.sampledAtMs&&source.timestampOrigin==SensorTimestampOrigin::SoftwareObservation)
            sensor.measurements.back().timestampOrigin=ResourceTimestampOrigin::SoftwareObservation;
        rejectNonFinite(sensor.measurements.back());
        const bool hasValue=sensor.measurements.back().value.has_value();
        sensor.facets.push_back({"read:"+source.metric,source.source,
            hasValue?"Value retained or freshness unknown; no write admission":"No valid value reported; no write admission",
            ResourceAvailability::Unknown,hasValue?ResourceEvidence::Observed:ResourceEvidence::Unknown});
        out.push_back(std::move(sensor));
    }
    return out;
}
ResourceCatalog observeMonitoringResources(const MonitoringSnapshot& snapshot){
    ResourceCatalog catalog;
    const auto extraMeasurement=[&](const char* metric,const char* unit,const char* retainedKey,
            const std::optional<double>& value,const std::string& source){
        ResourceMeasurement m{metric,unit,source,snapshot.processorStatus,value,0,std::nullopt};
        for(const auto& retained:snapshot.retainedForDisplay)if(retained.field==retainedKey){m.retained=true;m.sampledAtMs=retained.sampledAtMs;
            if(m.sampledAtMs)m.timestampOrigin=ResourceTimestampOrigin::DisplayCache;break;}
        return m;
    };
    for(std::size_t i=0;i<snapshot.processors.size();++i){
        const auto& p=snapshot.processors[i];
        const auto measurement=[&](const char* metric,const char* unit,const char* field,const std::optional<double>& value,const std::string& source){
            const std::optional<bool> freshness=snapshot.processorSampledAtMs?std::optional<bool>{false}:std::nullopt;
            ResourceMeasurement m{metric,unit,source,snapshot.processorStatus,value,snapshot.processorSampledAtMs,freshness};
            if(m.sampledAtMs)m.timestampOrigin=ResourceTimestampOrigin::CollectorStarted;
            const auto key="processor."+std::to_string(i)+"."+field;
            for(const auto& retained:snapshot.retainedForDisplay)if(retained.field==key){m.retained=true;m.sampledAtMs=retained.sampledAtMs;
                m.timestampOrigin=m.sampledAtMs?ResourceTimestampOrigin::DisplayCache:ResourceTimestampOrigin::Unknown;break;}
            return m;
        };
        // When adapter inventory exists it is authoritative for graphics rows;
        // the legacy cooling slot is not another independent GPU.
        if((i==0||!snapshot.gpuCards.initialized||snapshot.gpuCards.cards.empty())&&
           (p.celsius||p.usagePercent||p.clockMhz||p.powerWatts||!snapshot.processorModels[i].empty()||
            (i==0&&(snapshot.cpuCorePowerWatts||snapshot.socCelsius||snapshot.apuPackagePowerWatts))||
            (i==1&&(snapshot.gpuMemoryUsedBytes||snapshot.gpuMemoryTotalBytes)))){
            ResourceObservation cpu;cpu.kind=i==0?ResourceKind::Processor:ResourceKind::Graphics;
            cpu.presentationSlot=static_cast<unsigned>(i);
            cpu.name=snapshot.processorModels[i];cpu.identity.scope="monitoring presentation slot";
            const auto add=[&](const char* metric,const char* unit,const char* field,const std::optional<double>& value,const std::string& source){
                if(value)cpu.measurements.push_back(measurement(metric,unit,field,value,source));
            };
            add("temperature","°C","temperature",p.celsius,p.temperatureSource);add("usage","%","load",p.usagePercent,p.usageSource);
            add("frequency","MHz","clock",p.clockMhz,p.clockSource);add("power","W","power",p.powerWatts,p.powerSource);
            if(i==1){
                for(const auto& entry:{std::pair{"memory_used",snapshot.gpuMemoryUsedBytes},std::pair{"memory_total",snapshot.gpuMemoryTotalBytes}})
                    if(entry.second){ResourceMeasurement m{entry.first,"bytes",snapshot.gpuMemorySource};m.unsignedValue=entry.second;cpu.measurements.push_back(std::move(m));}
            }
            if(i==0){
                if(snapshot.cpuCorePowerWatts)cpu.measurements.push_back(extraMeasurement("core_power","W","cpu.cores_power",snapshot.cpuCorePowerWatts,snapshot.cpuCorePowerSource));
                if(snapshot.socCelsius)cpu.measurements.push_back(extraMeasurement("soc_temperature","°C","soc.temperature",snapshot.socCelsius,snapshot.socSource));
                if(snapshot.apuPackagePowerWatts)cpu.measurements.push_back(extraMeasurement("apu_package_power","W","apu.power",snapshot.apuPackagePowerWatts,snapshot.apuPackagePowerSource));
            }
            catalog.resources.push_back(std::move(cpu));
        }
        if(p.fanRpm||p.fanPwm||snapshot.nativeDefaultFans){
            ResourceObservation fan;fan.kind=ResourceKind::Fan;fan.identity.scope="monitoring presentation slot";
            fan.presentationSlot=static_cast<unsigned>(i);
            if(p.fanRpm)fan.measurements.push_back(measurement("speed","rpm","rpm",p.fanRpm,p.fanSource));
            if(p.fanPwm)fan.measurements.push_back(measurement("duty","%","pwm",p.fanPwm,p.fanSource));
            if(snapshot.nativeDefaultFans){const auto& native=snapshot.nativeDefaultFans->channels[i];
                fan.defaultFanCurve=native;
                const auto observed=[&](const char* metric,const char* unit,const std::optional<double>& value,const std::string& source){
                    ResourceMeasurement m{metric,unit,source,native.retained?"last confirmed native Default observation":native.forced?"EC forced override":"native Default observation",value,native.sampledAtMs,native.retained};
                    m.timestampOrigin=ResourceTimestampOrigin::SoftwareObservation;fan.measurements.push_back(std::move(m));};
                observed("control_temperature","°C",native.temperatureCelsius,native.temperatureSource);
                if(native.rpm){
                    observed("requested_speed","rpm",native.requestedRpm,native.requestSource);
                    observed("actual_speed","rpm",native.actualRpm,p.fanSource.empty()?"EC feedback / RPM":p.fanSource);
                }else observed("requested_duty","%",native.requestedPercent,native.requestSource);
                fan.facets.push_back({"read:native_default_curve",native.curveSource,"Firmware-decoded base request; not applied PWM or a writable Manual curve",ResourceAvailability::Available,ResourceEvidence::Documented});
            }
            catalog.resources.push_back(std::move(fan));
        }
    }
    if(snapshot.pchCelsius){
        ResourceObservation pch;pch.kind=ResourceKind::Chipset;pch.name="PCH";pch.presentationSlot=0;
        pch.identity.scope="monitoring presentation slot";
        pch.measurements.push_back(extraMeasurement("temperature","°C","pch.temperature",snapshot.pchCelsius,snapshot.pchSource));
        catalog.resources.push_back(std::move(pch));
    }
    if(snapshot.npuUsagePercent||snapshot.npuClockMhz||snapshot.npuPowerWatts){
        ResourceObservation npu;npu.kind=ResourceKind::Accelerator;npu.name="NPU";npu.presentationSlot=0;
        npu.identity.scope="monitoring presentation slot";
        if(snapshot.npuUsagePercent)npu.measurements.push_back(extraMeasurement("usage","%","npu.load",snapshot.npuUsagePercent,snapshot.npuUsageSource));
        if(snapshot.npuClockMhz)npu.measurements.push_back(extraMeasurement("frequency","MHz","npu.clock",snapshot.npuClockMhz,snapshot.npuClockSource));
        if(snapshot.npuPowerWatts)npu.measurements.push_back(extraMeasurement("power","W","npu.power",snapshot.npuPowerWatts,snapshot.npuPowerSource));
        catalog.resources.push_back(std::move(npu));
    }
    auto additional=observeSensorResources(snapshot.additionalSensors);
    for(auto& sensor:additional)catalog.resources.push_back(std::move(sensor));
    if(snapshot.gpuCards.initialized){
        for(std::size_t index=0;index<snapshot.gpuCards.cards.size();++index){
            const auto& card=snapshot.gpuCards.cards[index];
            ResourceObservation gpu;gpu.kind=ResourceKind::Graphics;gpu.name=card.name;
            gpu.presentationSlot=static_cast<unsigned>(index);
            if(!card.physicalId.empty()){gpu.identity.resource=card.physicalId;gpu.identity.scope="graphics provider physical identity";}
            else if(!card.adapterId.empty()){gpu.identity.resource=card.adapterId;gpu.identity.scope="OS graphics adapter identity; stability not established";}
            const auto add=[&](const char* metric,const char* unit,const char* cacheField,const std::optional<double>& value,const std::string& source,std::optional<bool> stale=std::nullopt){
                if(!value)return;
                ResourceMeasurement measurement{metric,unit,source,card.status,value,0,stale};
                // DisplayContinuity keys are scoped by adapterId. Empty IDs
                // cannot safely distinguish two adapters, so never borrow a
                // cached timestamp from their shared empty key.
                if(!card.adapterId.empty()){
                    const auto key="gpu."+card.adapterId+"."+cacheField;
                    for(const auto& retained:snapshot.retainedForDisplay)if(retained.field==key){
                        measurement.retained=true;measurement.sampledAtMs=retained.sampledAtMs;
                        if(measurement.sampledAtMs)measurement.timestampOrigin=ResourceTimestampOrigin::DisplayCache;break;
                    }
                }
                gpu.measurements.push_back(std::move(measurement));
            };
            add("temperature","°C","temperature",card.celsius,card.temperatureSource);
            // usageStale can identify a retained value, but false is also the
            // default when this presentation source has no sample timestamp.
            add("usage","%","load",card.usagePercent,card.usageSource,
                card.usageStale?std::optional<bool>{true}:std::nullopt);
            add("frequency","MHz","clock",card.clockMhz,card.clockSource);add("power","W","power",card.powerWatts,card.powerSource);
            for(const auto& entry:{std::pair{"memory_used",card.usedBytes},std::pair{"memory_total",card.totalBytes}})
                if(entry.second){ResourceMeasurement m{entry.first,"bytes",card.memorySource,card.status};m.unsignedValue=entry.second;gpu.measurements.push_back(std::move(m));}
            catalog.resources.push_back(std::move(gpu));
        }
    }
    const auto device=[&](const DeviceTemperature& d,ResourceKind kind,std::size_t slot,const char* prefix){
        if(!d.present&&!d.celsius&&d.model.empty()&&d.serial.empty())return;
        ResourceObservation item;item.kind=kind;item.present=d.present;item.name=d.model;item.sourceSerialHint=d.serial;item.identity.scope="monitoring presentation slot";item.presentationSlot=static_cast<unsigned>(slot);
        if(d.celsius){
            // This source does not report an individual sample time. Lack of a
            // retention marker alone cannot establish per-reading freshness.
            ResourceMeasurement m{"temperature","°C",d.source,d.status,d.celsius,0,std::nullopt};
            const auto key=std::string(prefix)+std::to_string(slot);
            for(const auto& old:snapshot.retainedForDisplay)if(old.field==key){m.retained=true;m.sampledAtMs=old.sampledAtMs;
                if(m.sampledAtMs)m.timestampOrigin=ResourceTimestampOrigin::DisplayCache;break;}
            item.measurements.push_back(std::move(m));
        }
        if(kind==ResourceKind::Memory&&d.present!=false&&snapshot.memoryClockMhz&&snapshot.memoryClockSampledAtMs){
            // The collector reports a common clock, not a DIMM/channel mapping.
            const auto addClock=[&](const char* metric,const char* unit,double value){
                ResourceMeasurement m{metric,unit,snapshot.memoryClockSource,snapshot.memoryClockStatus,value,
                    snapshot.memoryClockSampledAtMs,false};
                m.timestampOrigin=ResourceTimestampOrigin::SoftwareObservation;
                item.measurements.push_back(std::move(m));
            };
            addClock("frequency","MHz",*snapshot.memoryClockMhz);
            addClock("data_rate","MT/s",*snapshot.memoryClockMhz*2.);
        }
        catalog.resources.push_back(std::move(item));
    };
    for(std::size_t i=0;i<snapshot.memory.size();++i)device(snapshot.memory[i],ResourceKind::Memory,i,"memory.");
    for(std::size_t i=0;i<snapshot.disks.size();++i)device(snapshot.disks[i],ResourceKind::Storage,i,"disk.");
    if(snapshot.systemPowerWatts||snapshot.systemConsumptionWatts){
        ResourceObservation power;power.kind=ResourceKind::SystemPower;power.presentationSlot=0;
        power.name="System power";power.identity.scope="monitoring presentation slot";
        const std::optional<bool> retained=snapshot.systemPowerStale?std::optional<bool>{true}:std::nullopt;
        if(snapshot.systemPowerWatts)power.measurements.push_back({"reported_power","W",snapshot.systemPowerSource,
            "Experimental source; interpretation not confirmed",snapshot.systemPowerWatts,0,retained});
        if(snapshot.systemConsumptionWatts){
            std::string status="Estimate: reported power minus positive battery charging power";
            if(snapshot.battery.flow==BatteryFlow::Charging&&snapshot.battery.dynamicStale)status+="; battery reading stale";
            power.measurements.push_back({"estimated_consumption","W",snapshot.systemPowerSource,
                std::move(status),snapshot.systemConsumptionWatts,0,retained});
        }
        catalog.resources.push_back(std::move(power));
    }
    const auto addBattery=[&](const BatteryReading& b,unsigned slot){
      if(b.present||b.percent||b.watts||b.volts||b.celsius||b.cycles||b.healthPercent||!b.deviceId.empty()){
        ResourceObservation battery;battery.kind=ResourceKind::Battery;battery.present=b.present;battery.name=b.model;
        battery.presentationSlot=slot;
        if(!b.deviceId.empty()){battery.identity.resource=b.deviceId;battery.identity.scope="battery source device ID";}
        const auto add=[&](const char* name,const char* unit,const std::optional<double>& value,std::size_t field,const std::string& source){
            if(value){
                const std::optional<bool> freshness=b.dynamicStale?std::optional<bool>{true}:
                    b.dynamicAtMs[field]?std::optional<bool>{false}:std::nullopt;
                battery.measurements.push_back({name,unit,source,b.powerStatus,value,b.dynamicAtMs[field],freshness});
                if(b.dynamicAtMs[field])battery.measurements.back().timestampOrigin=ResourceTimestampOrigin::SoftwareObservation;
            }
        };
        add("charge","%",b.percent,0,b.source);add("power","W",b.watts,1,b.source);
        add("temperature","°C",b.celsius,2,b.temperatureSource);add("voltage","V",b.volts,3,b.source);
        add("current","mA",b.milliamps,4,b.source);
        if(b.cycles){ResourceMeasurement m{"cycles","count",b.source};m.unsignedValue=*b.cycles;battery.measurements.push_back(std::move(m));}
        if(b.healthPercent){const std::optional<bool> retained=b.healthStale?std::optional<bool>{true}:b.healthAtMs?std::optional<bool>{false}:std::nullopt;
            ResourceMeasurement m{"health","%",b.healthSource,b.powerStatus,b.healthPercent,b.healthAtMs,retained};
            if(b.healthAtMs)m.timestampOrigin=ResourceTimestampOrigin::SoftwareObservation;battery.measurements.push_back(std::move(m));}
        catalog.resources.push_back(std::move(battery));
      }
    };
    // Diagnostic inventory can contain multiple batteries. When unavailable,
    // keep the selected dashboard observation without manufacturing a second
    // copy or guessing identity by model name.
    if(snapshot.diagnostic&&!snapshot.batteries.empty()){
        for(unsigned i=0;i<snapshot.batteries.size();++i)addBattery(snapshot.batteries[i],i);
    }else addBattery(snapshot.battery,0);
    // Only describe observations already present. These facets cannot admit
    // setters, and a retained or freshness-unknown value is not evidence that
    // its source is currently available.
    for(auto& item:catalog.resources)if(item.kind!=ResourceKind::Sensor)for(auto& measurement:item.measurements){
        rejectNonFinite(measurement);
        const bool hasValue=measurement.value.has_value()||measurement.unsignedValue.has_value();
        const bool fresh=hasValue&&measurement.retained==false;
        item.facets.push_back({"read:"+measurement.metric,measurement.source,
            !hasValue?"No value reported; no write admission":fresh?"Observed value; no write admission":"Value retained or freshness unknown; no write admission",
            item.present==false?ResourceAvailability::Unavailable:fresh?ResourceAvailability::Available:ResourceAvailability::Unknown,
            hasValue?ResourceEvidence::Observed:ResourceEvidence::Unknown});
    }
    return catalog;
}
ResourceCatalog observeMachineResources(const BoardDescriptor& board,const MonitoringSnapshot& snapshot){
    auto result=observeMonitoringResources(snapshot);
    const auto& profile=deviceProfile(board);
    auto& machine=result.machine;
    machine.manufacturer=board.manufacturer;machine.board=board.baseBoard;machine.product=board.productName;
    machine.profile=profile.id;machine.protocol=profile.protocolId;machine.ecBackend=profile.ecBackendId;machine.smuBackend=profile.smuBackendId;
    machine.deviceClass=profile.deviceClass;
    machine.catalogRecognized=!profile.id.empty();
    for(const auto& declaration:declaredProfileFeatures(profile.capabilities))
        machine.declaredFeatures.push_back({std::string(declaration.name),"DeviceProfile",
            "Profile declaration only; runtime admission not established",ResourceAvailability::Unknown,ResourceEvidence::Documented});
    return result;
}
std::vector<std::string> compareMonitoringProjection(const MonitoringSnapshot& snapshot,const ResourceCatalog& catalog,std::size_t* observedValues){
    std::vector<std::string> mismatches;
    if(observedValues)*observedValues=0;
    const auto rowCheck=[&](ResourceKind kind,unsigned slot,bool expected,const std::string& name,const std::string& identity){
        const ResourceObservation* row=nullptr;unsigned matches=0;
        for(const auto& item:catalog.resources)if(item.kind==kind&&item.presentationSlot==slot){row=&item;++matches;}
        if(matches>1){mismatches.push_back("duplicate resource slot "+std::to_string(slot));return;}
        if(bool(row)!=expected){mismatches.push_back("resource presence slot "+std::to_string(slot));return;}
        if(row&&(row->name!=name||row->identity.resource!=identity))mismatches.push_back("resource identity slot "+std::to_string(slot));
    };
    const auto check=[&](ResourceKind kind,unsigned slot,const char* metric,const std::optional<double>& expected,const std::string& source){
        if(expected&&observedValues)++*observedValues;
        const ResourceObservation* row=nullptr;unsigned matches=0;
        for(const auto& resource:catalog.resources)if(resource.kind==kind&&resource.presentationSlot==slot){row=&resource;++matches;}
        if(matches>1){mismatches.push_back("duplicate slot "+std::to_string(slot));return;}
        const ResourceMeasurement* found=nullptr;
        if(row)for(const auto& item:row->measurements)if(item.metric==metric){if(found){mismatches.push_back("duplicate metric "+std::string(metric));return;}found=&item;}
        const bool same=found&&found->value&&expected&&(*found->value==*expected||
            (std::isnan(*found->value)&&std::isnan(*expected)));
        if((!found&&expected)||(found&&((found->value.has_value()!=expected.has_value())||(expected&&!same))))
            mismatches.push_back("slot "+std::to_string(slot)+" "+metric);
        else if(found&&expected&&found->source!=source)mismatches.push_back("slot "+std::to_string(slot)+" "+metric+" source");
    };
    for(unsigned i=0;i<snapshot.processors.size();++i){
        const auto& p=snapshot.processors[i];
        if(i==0||!snapshot.gpuCards.initialized||snapshot.gpuCards.cards.empty()){
            const auto kind=i==0?ResourceKind::Processor:ResourceKind::Graphics;
            rowCheck(kind,i,bool(p.celsius||p.usagePercent||p.clockMhz||p.powerWatts||!snapshot.processorModels[i].empty()||
                (i==0&&(snapshot.cpuCorePowerWatts||snapshot.socCelsius||snapshot.apuPackagePowerWatts))||
                (i==1&&(snapshot.gpuMemoryUsedBytes||snapshot.gpuMemoryTotalBytes))),snapshot.processorModels[i],{});
            check(kind,i,"temperature",p.celsius,p.temperatureSource);check(kind,i,"usage",p.usagePercent,p.usageSource);
            check(kind,i,"frequency",p.clockMhz,p.clockSource);check(kind,i,"power",p.powerWatts,p.powerSource);
        }
        rowCheck(ResourceKind::Fan,i,bool(p.fanRpm||p.fanPwm||snapshot.nativeDefaultFans),{},{});
        check(ResourceKind::Fan,i,"speed",p.fanRpm,p.fanSource);check(ResourceKind::Fan,i,"duty",p.fanPwm,p.fanSource);
    }
    check(ResourceKind::Processor,0,"core_power",snapshot.cpuCorePowerWatts,snapshot.cpuCorePowerSource);
    check(ResourceKind::Processor,0,"soc_temperature",snapshot.socCelsius,snapshot.socSource);
    check(ResourceKind::Processor,0,"apu_package_power",snapshot.apuPackagePowerWatts,snapshot.apuPackagePowerSource);
    rowCheck(ResourceKind::Chipset,0,bool(snapshot.pchCelsius),"PCH",{});
    check(ResourceKind::Chipset,0,"temperature",snapshot.pchCelsius,snapshot.pchSource);
    rowCheck(ResourceKind::Accelerator,0,bool(snapshot.npuUsagePercent||snapshot.npuClockMhz||snapshot.npuPowerWatts),"NPU",{});
    check(ResourceKind::Accelerator,0,"usage",snapshot.npuUsagePercent,snapshot.npuUsageSource);
    check(ResourceKind::Accelerator,0,"frequency",snapshot.npuClockMhz,snapshot.npuClockSource);
    check(ResourceKind::Accelerator,0,"power",snapshot.npuPowerWatts,snapshot.npuPowerSource);
    rowCheck(ResourceKind::SystemPower,0,bool(snapshot.systemPowerWatts||snapshot.systemConsumptionWatts),"System power",{});
    check(ResourceKind::SystemPower,0,"reported_power",snapshot.systemPowerWatts,snapshot.systemPowerSource);
    check(ResourceKind::SystemPower,0,"estimated_consumption",snapshot.systemConsumptionWatts,snapshot.systemPowerSource);
    for(unsigned i=0;i<snapshot.additionalSensors.size();++i){
        const auto& sensor=snapshot.additionalSensors[i];
        rowCheck(ResourceKind::Sensor,i,true,sensor.device,{});
        check(ResourceKind::Sensor,i,sensor.metric.c_str(),sensor.value,sensor.source);
    }
    if(snapshot.gpuCards.initialized&&!snapshot.gpuCards.cards.empty())for(unsigned i=0;i<snapshot.gpuCards.cards.size();++i){
        const auto& card=snapshot.gpuCards.cards[i];
        rowCheck(ResourceKind::Graphics,i,true,card.name,card.physicalId.empty()?card.adapterId:card.physicalId);
        check(ResourceKind::Graphics,i,"temperature",card.celsius,card.temperatureSource);
        check(ResourceKind::Graphics,i,"usage",card.usagePercent,card.usageSource);
        check(ResourceKind::Graphics,i,"frequency",card.clockMhz,card.clockSource);
        check(ResourceKind::Graphics,i,"power",card.powerWatts,card.powerSource);
    }
    for(unsigned i=0;i<snapshot.memory.size();++i){const auto& d=snapshot.memory[i];
        rowCheck(ResourceKind::Memory,i,bool(d.present||d.celsius||!d.model.empty()||!d.serial.empty()),d.model,{});
        for(const auto& item:catalog.resources)if(item.kind==ResourceKind::Memory&&item.presentationSlot==i&&item.sourceSerialHint!=d.serial)
            mismatches.push_back("resource serial hint slot "+std::to_string(i));
        check(ResourceKind::Memory,i,"temperature",d.celsius,d.source);}
    for(unsigned i=0;i<snapshot.disks.size();++i){const auto& d=snapshot.disks[i];
        rowCheck(ResourceKind::Storage,i,bool(d.present||d.celsius||!d.model.empty()||!d.serial.empty()),d.model,{});
        for(const auto& item:catalog.resources)if(item.kind==ResourceKind::Storage&&item.presentationSlot==i&&item.sourceSerialHint!=d.serial)
            mismatches.push_back("resource serial hint slot "+std::to_string(i));
        check(ResourceKind::Storage,i,"temperature",d.celsius,d.source);}
    const auto battery=[&](const BatteryReading& b,unsigned i){
        rowCheck(ResourceKind::Battery,i,bool(b.present||b.percent||b.watts||b.volts||b.celsius||b.cycles||b.healthPercent||!b.deviceId.empty()),b.model,b.deviceId);
        check(ResourceKind::Battery,i,"charge",b.percent,b.source);
        check(ResourceKind::Battery,i,"power",b.watts,b.source);
        check(ResourceKind::Battery,i,"temperature",b.celsius,b.temperatureSource);
        check(ResourceKind::Battery,i,"voltage",b.volts,b.source);
        check(ResourceKind::Battery,i,"current",b.milliamps,b.source);
        check(ResourceKind::Battery,i,"health",b.healthPercent,b.healthSource);
    };
    if(snapshot.diagnostic&&!snapshot.batteries.empty())for(unsigned i=0;i<snapshot.batteries.size();++i)battery(snapshot.batteries[i],i);
    else battery(snapshot.battery,0);
    // Detect stale rows after removal as well as missing rows after insertion.
    for(const auto& resource:catalog.resources){
        if(!resource.presentationSlot)continue;
        const auto slot=*resource.presentationSlot;bool inRange=true;
        switch(resource.kind){
        case ResourceKind::Processor:inRange=slot==0;break;
        case ResourceKind::Fan:inRange=slot<snapshot.processors.size();break;
        case ResourceKind::Graphics:inRange=snapshot.gpuCards.initialized&&!snapshot.gpuCards.cards.empty()?slot<snapshot.gpuCards.cards.size():slot==1;break;
        case ResourceKind::Memory:inRange=slot<snapshot.memory.size();break;
        case ResourceKind::Storage:inRange=slot<snapshot.disks.size();break;
        case ResourceKind::Battery:inRange=snapshot.diagnostic&&!snapshot.batteries.empty()?slot<snapshot.batteries.size():slot==0;break;
        case ResourceKind::Accelerator:case ResourceKind::Chipset:case ResourceKind::SystemPower:inRange=slot==0;break;
        case ResourceKind::Sensor:inRange=slot<snapshot.additionalSensors.size();break;
        default:break;
        }
        if(!inRange)mismatches.push_back("unexpected resource slot "+std::to_string(slot));
    }
    // A matching number alone is insufficient: retained values, unknown
    // freshness and units must remain attached to the original observation.
    // This is a comparison only; it does not grant access to any provider.
    const auto reference=observeMonitoringResources(snapshot);
    for(const auto& row:catalog.resources){
        if(!row.presentationSlot){mismatches.push_back("resource missing presentation slot");continue;}
        const ResourceObservation* expectedRow=nullptr;
        for(const auto& item:reference.resources)
            if(item.kind==row.kind&&item.presentationSlot==row.presentationSlot){expectedRow=&item;break;}
        if(!expectedRow){mismatches.push_back("unexpected resource slot "+std::to_string(*row.presentationSlot));continue;}
        if(row.identity!=expectedRow->identity||row.owner!=expectedRow->owner||row.ownerSource!=expectedRow->ownerSource||
            row.sourceSerialHint!=expectedRow->sourceSerialHint||row.present!=expectedRow->present)
            mismatches.push_back("resource metadata slot "+std::to_string(*row.presentationSlot));
        if(expectedRow&&row.facets!=expectedRow->facets)
            mismatches.push_back("resource facets slot "+std::to_string(*row.presentationSlot));
        if(row.defaultFanCurve!=expectedRow->defaultFanCurve)
            mismatches.push_back("native Default fan projection slot "+std::to_string(*row.presentationSlot));
        // Null-valued observations can still carry source, status and time.
        // Compare in both directions: losing such a metric must not pass as
        // parity merely because its numeric value was already unknown.
        if(expectedRow)for(const auto& expected:expectedRow->measurements){
            bool present=false;
            for(const auto& actual:row.measurements)if(actual.metric==expected.metric){present=true;break;}
            if(!present)mismatches.push_back("missing metric "+expected.metric);
        }
        for(const auto& measurement:row.measurements){
            const ResourceMeasurement* expectedMeasurement=nullptr;
            if(expectedRow)for(const auto& item:expectedRow->measurements)
                if(item.metric==measurement.metric){expectedMeasurement=&item;break;}
            if(!expectedMeasurement){mismatches.push_back("unexpected metric "+measurement.metric);continue;}
            const bool rawMatches=measurement.raw.has_value()==expectedMeasurement->raw.has_value()&&
                (!measurement.raw||*measurement.raw==*expectedMeasurement->raw||
                    (std::isnan(*measurement.raw)&&std::isnan(*expectedMeasurement->raw)));
            if(measurement.unit!=expectedMeasurement->unit||measurement.source!=expectedMeasurement->source||
               measurement.status!=expectedMeasurement->status||!rawMatches||
               measurement.sampledAtMs!=expectedMeasurement->sampledAtMs||measurement.timestampOrigin!=expectedMeasurement->timestampOrigin||
               measurement.retained!=expectedMeasurement->retained||measurement.unsignedValue!=expectedMeasurement->unsignedValue)
                mismatches.push_back("metric metadata "+measurement.metric);
        }
    }
    return mismatches;
}
ResourceProjectionCheck verifyMonitoringProjection(const MonitoringSnapshot& snapshot,const ResourceCatalog& catalog){
    ResourceProjectionCheck result;result.mismatches=compareMonitoringProjection(snapshot,catalog,&result.comparedValues);return result;
}
void ResourceState::update(const BoardDescriptor& board,const MonitoringSnapshot& snapshot) noexcept{
    try{
        auto next=std::make_shared<ResourceCatalog>(observeMachineResources(board,snapshot));
        auto revision=std::make_shared<unsigned char>(0);
        revision_.reset();current_=std::move(next);revision_=std::move(revision);
    }catch(...){clear();}
}
ResourcePresentationView::ResourcePresentationView(const ResourceState& state) noexcept
    :catalog_(state.current_),revision_(state.revision_){}
const ResourceMeasurement* ResourcePresentationView::measurement(ResourceKind kind,unsigned slot,std::string_view metric) const noexcept{
    if(!available())return nullptr;
    for(const auto& row:catalog_->resources)if(row.kind==kind&&row.presentationSlot==slot)
        for(const auto& m:row.measurements)if(m.metric==metric)return &m;
    return nullptr;
}
const NativeDefaultFanChannel* ResourcePresentationView::defaultFanCurve(unsigned slot) const noexcept{
    if(!available())return nullptr;
    for(const auto& row:catalog_->resources)if(row.kind==ResourceKind::Fan&&row.presentationSlot==slot&&row.defaultFanCurve)
        return nativeDefaultFanFresh(*row.defaultFanCurve)?&*row.defaultFanCurve:nullptr;
    return nullptr;
}
}
