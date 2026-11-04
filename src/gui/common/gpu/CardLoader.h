#pragma once
#include "IntelCard.h"
#include "NvidiaCard.h"
#include "AmdCard.h"
#include "MagicControl/ResourceCatalog.h"
namespace mc::gui::ui::gpu {
inline ProcessorCard loadCard(const MonitoringSnapshot& sample,const FanReadout& fan,const std::string& fanSource,const std::string& /*displayDevice*/,const ResourcePresentationView* resources){
    std::string reason;const auto* selected=gpuLoadCard(sample.gpuCards,reason);GpuCardReading fallback;
    if(!selected&&!sample.gpuCards.initialized){
        // Preview/older unbound sources retain their existing Wujie data. A
        // real populated catalogue must NOT borrow another adapter's sensors.
        const auto& p=sample.processors[1];fallback.name=sample.processorModels[1];fallback.celsius=p.celsius;fallback.usagePercent=p.usagePercent;fallback.clockMhz=p.clockMhz;fallback.powerWatts=p.powerWatts;
        fallback.temperatureSource=p.temperatureSource;fallback.usageSource=p.usageSource;fallback.clockSource=p.clockSource;fallback.powerSource=p.powerSource;
        fallback.usedBytes=sample.gpuMemoryUsedBytes;fallback.totalBytes=sample.gpuMemoryTotalBytes;fallback.memorySource=sample.gpuMemorySource;
        fallback.vendorId=sample.processorVendors[1]==ProcessorVendor::Nvidia?0x10de:sample.processorVendors[1]==ProcessorVendor::Intel?0x8086:sample.processorVendors[1]==ProcessorVendor::Amd?0x1002:0;
        fallback.legacyApu=fallback.vendorId==0x1002;selected=&fallback;reason="Прежний источник телеметрии GPU";
    }
    // Select exactly as before, then read only that row's accepted resource metrics.
    // Slot is a presentation index, not a physical identity or control handle.
    GpuCardReading display;
    if(selected){
        unsigned slot=1;
        if(selected!=&fallback)slot=static_cast<unsigned>(selected-sample.gpuCards.cards.data());
        display=*selected;
        const auto assign=[&](std::string_view metric,std::optional<double>& value,std::string& source){
            const auto* m=resources?resources->measurement(ResourceKind::Graphics,slot,metric):nullptr;
            value=m?m->value:std::nullopt;if(m)source=m->source;
        };
        assign("temperature",display.celsius,display.temperatureSource);assign("usage",display.usagePercent,display.usageSource);
        assign("frequency",display.clockMhz,display.clockSource);assign("power",display.powerWatts,display.powerSource);
        display.usedBytes=resourceDisplayUnsigned(resources,ResourceKind::Graphics,slot,"memory_used");
        display.totalBytes=resourceDisplayUnsigned(resources,ResourceKind::Graphics,slot,"memory_total");
        selected=&display;
    }
    ProcessorCard card;
    if(!selected){fallback.status=reason;card=baseCard(fallback,fan,fanSource);card.information[1]=reason;card.secondaryValue="—";card.secondarySource="Нет дополнительных данных";}
    else if(selected->vendorId==0x8086)card=intelCard(*selected,fan,fanSource);
    else if(selected->vendorId==0x10de)card=nvidiaCard(*selected,fan,fanSource);
    else if(selected->vendorId==0x1002)card=amdCard(*selected,sample,fan,fanSource,resources);
    else card=baseCard(*selected,fan,fanSource);
    card.selectionReason=reason;
    if(sample.gpuCards.hybridAvailable||(selected&&selected->vendorId==0x8086)){
        card.sources[0]="физический канал 2 · "+card.sources[0];card.sources[1]="физический канал 2 · "+card.sources[1];
    }
    if(sample.gpuCards.hybridAvailable)card.title.insert(3," · гибрид");
    return card;
}
}
