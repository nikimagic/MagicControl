#include "MagicControl/ActionLog.h"
#include "MagicControl/GpuDevice.h"
#include "MagicControl/NvidiaGpu.h"
#include <stdexcept>
#include <algorithm>
#include <map>
namespace mc {
namespace {
int powerPriority(GpuPowerDomain domain){switch(domain){case GpuPowerDomain::Board:return 4;case GpuPowerDomain::Asic:return 3;case GpuPowerDomain::Gpu:return 2;case GpuPowerDomain::Gfx:return 1;default:return 0;}}
}
std::vector<GpuReading> physicalGpuReadings(std::vector<GpuReading> readings){
    std::vector<GpuReading> result;std::map<std::string,std::size_t> physical;
    for(auto& row:readings){
        if(row.providerIdentities.empty())row.providerIdentities.push_back({row.uuid,row.source,row.status});
        if(row.physicalId.empty()){result.push_back(std::move(row));continue;}
        const auto [entry,inserted]=physical.emplace(row.physicalId,result.size());
        if(inserted){result.push_back(std::move(row));continue;}
        auto& canonical=result[entry->second];
        // Prefer a control-capable original provider handle, then an available
        // observation. Do not manufacture tuning permission from a PMLog row.
        if((!canonical.powerControl&&row.powerControl)||(!canonical.powerControl&&!canonical.available&&row.available))std::swap(canonical,row);
        canonical.providerIdentities.insert(canonical.providerIdentities.end(),row.providerIdentities.begin(),row.providerIdentities.end());
        canonical.additionalSensors.insert(canonical.additionalSensors.end(),row.additionalSensors.begin(),row.additionalSensors.end());
        canonical.apiStatus.push_back("same physical GPU observation: "+row.source+" handle="+row.uuid);
        if(!row.source.empty()&&canonical.source.find(row.source)==std::string::npos)canonical.source+=" / "+row.source;
        const auto missing=[](auto& target,const auto& source){if(!target)target=source;};
        if(!canonical.temperature&&row.temperature){canonical.temperature=row.temperature;canonical.temperatureSource=row.temperatureSource;}
        missing(canonical.utilization,row.utilization);missing(canonical.graphicsMhz,row.graphicsMhz);missing(canonical.memoryMhz,row.memoryMhz);
        missing(canonical.fanRpm,row.fanRpm);missing(canonical.hotspotCelsius,row.hotspotCelsius);missing(canonical.memoryCelsius,row.memoryCelsius);missing(canonical.memoryJunctionCelsius,row.memoryJunctionCelsius);
        missing(canonical.totalBytes,row.totalBytes);missing(canonical.usedBytes,row.usedBytes);missing(canonical.freeBytes,row.freeBytes);
        // Domains are alternatives, never additive. A supported zero remains a
        // real observation; a nonzero lower-priority domain does not replace it.
        if(row.powerMw&&(!canonical.powerMw||powerPriority(row.powerDomain)>powerPriority(canonical.powerDomain))){canonical.powerMw=row.powerMw;canonical.powerDomain=row.powerDomain;canonical.powerSource=row.powerSource;}
    }
    return result;
}
bool radeonDiscretePresent() noexcept {try{for(const auto& r:graphicsAdapters())if(r.vendorId==0x1002&&r.type==GpuType::Discrete&&!r.software)return true;}catch(...){}return false;}
struct DiscreteGpu::Impl {GpuVendor vendor;NvidiaGpu nvidia;RadeonGpu radeon;explicit Impl(GpuVendor v):vendor(v){}};
DiscreteGpu::DiscreteGpu(GpuVendor v):impl_(std::make_unique<Impl>(v)){}
DiscreteGpu::~DiscreteGpu()=default;
GpuVendor DiscreteGpu::vendor()const{return impl_->vendor;}
std::vector<GpuReading> DiscreteGpu::read(){
    try{
    if(impl_->vendor==GpuVendor::Radeon)return physicalGpuReadings(impl_->radeon.read());
    if(!nvidiaGpuPresent()){GpuReading r;r.name="NVIDIA";r.deviceAbsent=true;r.status="Видеокарта NVIDIA не обнаружена ОС";return {r};}
    return physicalGpuReadings(impl_->nvidia.read());
    }catch(const std::exception& e){GpuReading r;r.name=gpuVendorName(impl_->vendor);r.status=e.what();return {r};}
}
void DiscreteGpu::setPowerLimit(const std::string& id,int value){
    audit::Scope trace("gpu.set_limit","\"device_id\":"+audit::quote(id)+",\"watts\":"+std::to_string(value));
    if(impl_->vendor==GpuVendor::Radeon)impl_->radeon.setPowerLimit(id,value);
    else{if(value<0||value>100000)throw std::invalid_argument("Недопустимый лимит NVIDIA");impl_->nvidia.setPowerLimit(id,static_cast<unsigned>(value)*1000);}
}
void DiscreteGpu::resetPowerLimit(const std::string& id){audit::Scope trace("gpu.reset_limit","\"device_id\":"+audit::quote(id));if(impl_->vendor==GpuVendor::Radeon)impl_->radeon.resetPowerLimit(id);else impl_->nvidia.resetPowerLimit(id);}
}
