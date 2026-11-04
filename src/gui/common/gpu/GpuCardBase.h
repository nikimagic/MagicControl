#pragma once
#include "ProcessorCardModel.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/FanTelemetry.h"
namespace mc::gui::ui::gpu {
inline std::string source(const std::string& text){return text.empty()?"нет данных":text;}
inline ProcessorCard baseCard(const GpuCardReading& gpu,const FanReadout& fan,const std::string& fanSource){
    ProcessorCard card;card.title="GPU";if(!gpu.name.empty())card.title+=" · "+gpu.name;
    card.values={monitoringGrouped(fan.rpmAvailable?std::optional<double>(fan.rpm):std::nullopt),monitoringMetric(fan.pwmAvailable?std::optional<double>(fan.dutyPercent):std::nullopt),monitoringMetric(gpu.celsius,1,true)};
    // FanReadout remains physical EC channel2/shared cooling. It is never taken
    // from the displayed Intel adapter's driver/fan telemetry.
    card.sources={source(fanSource),source(fanSource),gpu.temperatureSource.empty()?"Источник недоступен":gpu.temperatureSource};
    const bool gfx=gpu.powerSource.find("PMLog/30")!=std::string::npos;
    card.summary={{{"Загрузка ",monitoringMetric(gpu.usagePercent,1,true)+"%"},{"",monitoringGrouped(gpu.clockMhz)+" МГц"},{gfx?"GFX ":"GPU ",monitoringMetric(gpu.powerWatts,2)+" Вт"}}};
    for(const auto& metric:card.summary){if(!card.information[0].empty())card.information[0]+=" · ";card.information[0]+=metric.label+metric.value;}
    card.information[1]="Загрузка / частота: "+source(gpu.usageSource);
    card.information[2]="Мощность: "+source(gpu.powerSource);return card;
}
inline void memory(ProcessorCard& card,const GpuCardReading& gpu,const std::string& title){
    const auto gib=[](const std::optional<std::uint64_t>& n)->std::optional<double>{return n?std::optional<double>(*n/1073741824.):std::nullopt;};
    card.secondaryTitle=title;card.secondaryValue=monitoringMetric(gib(gpu.usedBytes),2);card.secondaryUnit="ГиБ";
    card.secondaryExtraTitle="Всего";card.secondaryExtraValue=monitoringMetric(gib(gpu.totalBytes),2);card.secondaryExtraUnit="ГиБ";card.secondarySource=source(gpu.memorySource);
}
}
