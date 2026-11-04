#pragma once
#include "GpuCardBase.h"
#include "MagicControl/ResourceCatalog.h"
namespace mc::gui::ui::gpu {
inline ProcessorCard amdCard(const GpuCardReading& reading,const MonitoringSnapshot& sample,const FanReadout& fan,const std::string& fanSource,const ResourcePresentationView* resources){
    auto card=baseCard(reading,fan,fanSource);
    // Preserve the exact Wujie APU/NPU secondary slots; a discrete Radeon gets
    // its own bound VRAM instead of borrowing another GPU or CPU Package.
    if(reading.legacyApu||(reading.type==GpuType::Integrated&&sample.processorVendors[0]==ProcessorVendor::Amd)){
        const auto* clock=resources?resources->measurement(ResourceKind::Accelerator,0,"frequency"):nullptr;
        const auto* power=resources?resources->measurement(ResourceKind::Accelerator,0,"power"):nullptr;
        card.secondaryTitle="NPU";card.secondaryValue=monitoringMetric(resourceDisplayValue(resources,ResourceKind::Accelerator,0,"usage"),0,true);card.secondaryUnit="%";
        card.secondarySource=monitoringGrouped(resourceDisplayValue(resources,ResourceKind::Accelerator,0,"frequency"))+" МГц · "+monitoringMetric(resourceDisplayValue(resources,ResourceKind::Accelerator,0,"power"),2)+" Вт\n"+source(clock?clock->source:sample.npuClockSource)+" · "+source(power?power->source:sample.npuPowerSource);
    }else memory(card,reading,"VRAM");
    return card;
}
}
