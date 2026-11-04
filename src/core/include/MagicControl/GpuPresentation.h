#pragma once
#include "GpuDevice.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <string_view>
#include <tuple>
namespace mc {
// Presentation only. These rows never replace the fixed physical cooling
// sources in MonitoringSnapshot::processors or grant a vendor control handle.
struct GpuCardReading {
    std::string adapterId,physicalId,providerId,name,status;
    unsigned vendorId{};GpuType type{GpuType::Unknown};
    std::optional<double> celsius,usagePercent,clockMhz,powerWatts;
    std::optional<std::uint64_t> usedBytes,totalBytes;
    std::string temperatureSource,usageSource,clockSource,powerSource,memorySource;
    bool telemetryBound{},legacyApu{},usageStale{};
    friend bool operator==(const GpuCardReading&,const GpuCardReading&)=default;
};
struct GpuDisplayRoute {
    std::string displayDevice,adapterId,targetAdapterId,source,status;
    bool primary{};
    friend bool operator==(const GpuDisplayRoute&,const GpuDisplayRoute&)=default;
};
struct GpuProviderReading {unsigned vendorId{};GpuReading reading;std::uint64_t sampledAtMs{};};
// Integrated Radeon GPUPower/ASIC is not proven graphics-only. Keep the raw
// provider reading, but display only an explicitly identified GFX domain. Never
// subtract CPU/package counters, sum domains, or clamp one sensor to another.
inline std::pair<std::optional<double>,std::string> gpuDisplayPower(
        const GpuReading& row,unsigned vendor,GpuType physicalType) {
    if(vendor==0x8086)return {};
    if(vendor==0x1002&&physicalType==GpuType::Integrated){
        if(row.powerDomain==GpuPowerDomain::Gfx&&row.powerMw)
            return {*row.powerMw/1000.,row.powerSource};
        for(const auto& sensor:row.additionalSensors)
            if(sensor.metric=="power/gfx"&&sensor.unit=="W"&&sensor.value&&
               std::isfinite(*sensor.value)&&*sensor.value>=0&&*sensor.value<=2000)
                return {sensor.value,sensor.source+" · GFX"};
        return {};
    }
    return {row.powerMw?std::optional<double>(*row.powerMw/1000.):std::nullopt,
        row.powerSource.empty()?row.source:row.powerSource};
}
struct GpuPresentationSnapshot {
    std::vector<GpuCardReading> cards;
    std::vector<GpuDisplayRoute> routes;
    // Preserve provider handles/raw diagnostics independently of the selected
    // card. Equality follows visible data, not diagnostic-only row ordering.
    std::vector<GpuProviderReading> rawReadings;
    std::string status,selectedAdapterId,selectionReason;
    bool initialized{};
    bool hybridAvailable{}; // OS iGPU+dGPU inventory, NOT a firmware MUX claim.
    friend bool operator==(const GpuPresentationSnapshot& a,const GpuPresentationSnapshot& b){return std::tie(a.cards,a.routes,a.status,a.selectedAdapterId,a.selectionReason,a.hybridAvailable,a.initialized)==std::tie(b.cards,b.routes,b.status,b.selectedAdapterId,b.selectionReason,b.hybridAvailable,b.initialized);}
};
inline std::optional<std::array<unsigned,4>> gpuPciLocation(std::string_view text){
    // NVML exposes full domain:bus:device.function. Do not match friendly names.
    std::array<unsigned,4> values{};std::size_t start{};
    for(int i=0;i<4;++i){const auto end=i==3?text.size():text.find(i==2?'.':':',start);
        if(end==std::string_view::npos||end==start)return {};
        const auto parsed=std::from_chars(text.data()+start,text.data()+end,values[i],16);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+end)return {};start=end+1;}
    if(values[1]>255||values[2]>31||values[3]>7)return {};return values;
}
inline std::string gpuProviderAdapterId(unsigned vendor,const GpuReading& row,const std::vector<GraphicsAdapter>& adapters){
    const auto direct=row.osAdapterId.empty()?row.uuid:row.osAdapterId;
    const auto exact=std::find_if(adapters.begin(),adapters.end(),[&](const auto& a){return !a.software&&a.vendorId==vendor&&a.id==direct;});
    if(exact!=adapters.end())return exact->id;
    if(!row.physicalId.empty()){
        const auto physical=std::find_if(adapters.begin(),adapters.end(),[&](const auto& a){return !a.software&&a.vendorId==vendor&&a.physicalId==row.physicalId;});
        if(physical!=adapters.end())return physical->id;
    }
    const auto pci=gpuPciLocation(row.pci);if(!pci)return {};
    const GraphicsAdapter* match{};unsigned count{};
    for(const auto& a:adapters){if(a.software||a.vendorId!=vendor||a.physicalId.empty()||!a.location.bus||!a.location.device||!a.location.function)continue;
        // Windows does not report a segment here. Accept only segment0 and a
        // unique present full bus/device/function; ambiguity stays unbound.
        if((a.location.domain&&*a.location.domain!=(*pci)[0])||(!a.location.domain&&(*pci)[0]!=0))continue;
        if(*a.location.bus==(*pci)[1]&&*a.location.device==(*pci)[2]&&*a.location.function==(*pci)[3]){match=&a;++count;}}
    return count==1?match->id:std::string{};
}
inline const GpuCardReading* gpuLoadCard(const GpuPresentationSnapshot& catalog,std::string& reason){
    const GpuCardReading* selected{};
    const auto rank=[](const GpuCardReading& card){
        const bool valid=card.usagePercent&&std::isfinite(*card.usagePercent)&&*card.usagePercent>=0&&*card.usagePercent<=100;
        return std::tuple{valid&&!card.usageStale,valid?*card.usagePercent:-1.,card.type==GpuType::Discrete};
    };
    for(const auto& card:catalog.cards)
        if(!selected||rank(card)>rank(*selected)||(rank(card)==rank(*selected)&&card.adapterId<selected->adapterId))selected=&card;
    if(!selected){reason="Нет адаптеров GPU";return nullptr;}
    if(std::get<0>(rank(*selected)))reason="Максимальная доступная загрузка GPU · "+selected->usageSource;
    else reason=selected->usageStale?"Последняя подтверждённая загрузка GPU · "+selected->usageSource:"Загрузка GPU недоступна · выбран доступный адаптер";
    return selected;
}
inline GpuPresentationSnapshot gpuPresentation(const std::vector<GraphicsAdapter>& adapters,std::vector<GpuProviderReading> readings,std::vector<GpuDisplayRoute> routes,std::uint64_t now=0){
    GpuPresentationSnapshot out;out.initialized=true;out.routes=std::move(routes);out.rawReadings=std::move(readings);
    bool integrated{},discrete{};
    std::map<std::string,std::string> aliases;
    for(const auto& a:adapters){if(a.software)continue;GpuCardReading card;
        bool liveStatus{};
        card.adapterId=a.id;card.physicalId=a.physicalId;card.name=a.name;card.vendorId=a.vendorId;card.type=a.type;card.status=a.status;
        card.totalBytes=a.dedicatedBytes;card.memorySource="ОС · выделенная память адаптера";
        for(const auto& provider:out.rawReadings){const auto& row=provider.reading;
            if(gpuProviderAdapterId(provider.vendorId,row,adapters)!=a.id&&
               (a.physicalId.empty()||row.physicalId!=a.physicalId||provider.vendorId!=a.vendorId))continue;
            card.telemetryBound=true;if(card.providerId.empty())card.providerId=row.uuid;
            if(!row.name.empty())card.name=row.name;if(row.type!=GpuType::Unknown)card.type=row.type;
            if(!row.available){if(!liveStatus&&!row.status.empty())card.status=row.status;continue;}
            if(now&&provider.sampledAtMs&&(now<provider.sampledAtMs||now-provider.sampledAtMs>10000)){if(!liveStatus)card.status=row.status+" · provider telemetry stale";continue;}
            const bool hasMetrics=row.temperature||row.utilization||row.graphicsMhz||row.powerMw||row.usedBytes;
            if(!liveStatus&&!row.status.empty())card.status=row.status;
            liveStatus|=hasMetrics;
            const auto fill=[](auto& to,const auto& from){if(!to&&from)to=from;};
            if(!card.celsius&&row.temperature){card.celsius=*row.temperature;card.temperatureSource=row.temperatureSource.empty()?row.source:row.temperatureSource;}
            if(!card.usagePercent&&row.utilization){card.usagePercent=*row.utilization;card.usageSource=row.source;}
            if(!card.clockMhz&&row.graphicsMhz){card.clockMhz=*row.graphicsMhz;card.clockSource=row.source;}
            // Intel's legacy IGCL energy domain may correlate with CPU Package.
            // Raw evidence remains above; do not turn it into iGPU watts.
            if(!card.powerWatts){const auto [watts,source]=gpuDisplayPower(row,a.vendorId,a.type);
                card.powerWatts=watts;if(watts)card.powerSource=source;}
            fill(card.usedBytes,row.usedBytes);if(row.totalBytes)card.totalBytes=row.totalBytes;
            if(row.usedBytes||row.totalBytes)card.memorySource=row.source;
        }
        if(a.vendorId==0x8086)card.powerSource="Intel GPU: домен мощности не подтверждён";
        const auto same=card.physicalId.empty()?out.cards.end():std::find_if(out.cards.begin(),out.cards.end(),[&](const auto& c){return c.vendorId==card.vendorId&&c.physicalId==card.physicalId;});
        if(same!=out.cards.end()){
            aliases[card.adapterId]=same->adapterId;
            if(same->type==GpuType::Unknown)same->type=card.type;
            const auto missing=[](auto& to,const auto& from,auto& toSource,const auto& fromSource){if(!to&&from){to=from;toSource=fromSource;}};
            missing(same->celsius,card.celsius,same->temperatureSource,card.temperatureSource);
            missing(same->usagePercent,card.usagePercent,same->usageSource,card.usageSource);
            missing(same->clockMhz,card.clockMhz,same->clockSource,card.clockSource);
            missing(same->powerWatts,card.powerWatts,same->powerSource,card.powerSource);
            if(!same->usedBytes)same->usedBytes=card.usedBytes;
            if(!same->totalBytes)same->totalBytes=card.totalBytes;
            same->telemetryBound|=card.telemetryBound;
        }else out.cards.push_back(std::move(card));
    }
    // Raw OS/provider rows stay available in diagnostics; only presentation
    // aliases collapse, and display routes still resolve to the same device.
    for(auto& route:out.routes){if(const auto a=aliases.find(route.adapterId);a!=aliases.end())route.adapterId=a->second;
        if(const auto a=aliases.find(route.targetAdapterId);a!=aliases.end())route.targetAdapterId=a->second;}
    for(const auto& card:out.cards){integrated|=card.type==GpuType::Integrated;discrete|=card.type==GpuType::Discrete;}
    out.hybridAvailable=integrated&&discrete;
    out.status="Выбор по максимальной доступной загрузке GPU";
    if(out.hybridAvailable)out.status+=" · ОС видит iGPU+dGPU; режим MUX не подтверждён";
    if(const auto* selected=gpuLoadCard(out,out.selectionReason))out.selectedAdapterId=selected->adapterId;
    return out;
}
inline const GpuCardReading* gpuDisplayCard(const GpuPresentationSnapshot& catalog,const std::string& displayDevice,std::string& reason){
    const GpuDisplayRoute* route{};unsigned matches{};
    const auto same=[](const std::string& a,const std::string& b){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i])))return false;return true;};
    for(const auto& r:catalog.routes)if(!displayDevice.empty()?same(r.displayDevice,displayDevice):r.primary){route=&r;++matches;}
    if(matches!=1){reason=displayDevice.empty()?"Маршрут основного дисплея неизвестен":"Маршрут дисплея окна неизвестен";return nullptr;}
    const auto found=std::find_if(catalog.cards.begin(),catalog.cards.end(),[&](const auto& c){return c.adapterId==route->adapterId;});
    if(found==catalog.cards.end()){reason="Адаптер маршрута отсутствует в текущем каталоге";return nullptr;}
    reason=route->source+" · "+route->displayDevice;
    if(!route->status.empty())reason+=" · "+route->status;
    return &*found;
}
}
