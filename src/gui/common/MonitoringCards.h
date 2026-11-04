#pragma once
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include "MagicControl/Monitoring.h"
#include "MagicControl/ResourceCatalog.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/FanTelemetry.h"
#include "MetricLine.h"
#include "DisplayFormat.h"
#include "gpu/ProcessorCardModel.h"
#include "gpu/CardLoader.h"

namespace mc::gui::ui {
inline bool monitorBatteryVisible(const DeviceProfile& profile,const MonitoringSnapshot& sample){
    return !profileResourceAbsent(profile,"battery")||sample.battery.present==true;
}
}

namespace mc::gui::ui {
using mc::monitoringFanReading;
struct MonitoringCard {
    std::string title, value, unit, detail, source;
    BatteryFlow flow{BatteryFlow::Unknown};
};
inline MonitoringCard monitoringCard(const MonitoringSnapshot& snapshot, int index,const ResourcePresentationView* resources) {
    if (index == 0) {
        const auto& b = snapshot.battery;
        const auto percent=resourceDisplayValue(resources,ResourceKind::Battery,0,"charge");
        const auto watts=resourceDisplayValue(resources,ResourceKind::Battery,0,"power");
        MonitoringCard card{"Батарея", percent ? monitoringNumber(*percent, 0) : "—", "%", "", b.source, b.flow};
        card.detail = b.present == false ? "Не установлена" :
            b.flow == BatteryFlow::Charging ? "Зарядка" :
            b.flow == BatteryFlow::Discharging ? "Разрядка" :
            b.flow == BatteryFlow::Idle ? (b.onlineKnown ? (b.online ? "От сети" : "От батареи") : "Питание неизвестно") : "Нет показаний";
        if (watts) card.detail += " · " + monitoringNumber(std::abs(*watts), 1) + " Вт";
        if (card.source.empty()) card.source = "Источник не подключён";
        return card;
    }
    const bool memory = index < 3;
    const auto& d = memory ? snapshot.memory[index - 1] : snapshot.disks[index - 3];
    const auto* temperature=resources?resources->measurement(memory?ResourceKind::Memory:ResourceKind::Storage,
        static_cast<unsigned>(memory?index-1:index-3),"temperature"):nullptr;
    const auto value=resourceDisplayValue(resources,memory?ResourceKind::Memory:ResourceKind::Storage,
        static_cast<unsigned>(memory?index-1:index-3),"temperature");
    MonitoringCard card{std::string(memory ? "Память " : "Диск ") + std::to_string(memory ? index : index - 2),
        value ? monitoringNumber(*value, memory ? 1 : 0) : "—", "°C", "", temperature?temperature->source:d.source};
    card.detail = memory ? (!d.serial.empty()?"S/N: "+d.serial:!d.model.empty()?d.model:"S/N: —") :
        d.present == false ? "Не установлен" : !d.model.empty() ? d.model :
        !d.serial.empty() ? "S/N: " + d.serial : d.present == true ? "Название недоступно" : "Нет доступных данных";
    if (card.source.empty()) card.source = "Источник не подключён";
    if(memory){const auto rate=resourceDisplayValue(resources,ResourceKind::Memory,static_cast<unsigned>(index-1),"data_rate");
        if(rate){const auto* reading=resources?resources->measurement(ResourceKind::Memory,static_cast<unsigned>(index-1),"data_rate"):nullptr;
            card.source=(reading&&reading->status.starts_with("approximate")?"≈ ":"")+monitoringNumber(*rate,0)+" MT/s · "+card.source;}}
    return card;
}
struct BatteryCard {
    std::array<InlineMetric, 3> left;
    std::array<std::string, 3> right;
    BatteryFlow flow{BatteryFlow::Unknown};
};
// The collector expires its retained EC sample after 10 seconds. The footer
// may show that short-lived observation, explicitly marked as retained.
inline std::optional<double> displayedSystemConsumption(const MonitoringSnapshot& snapshot,
                                                         const ResourcePresentationView* resources){
    (void)snapshot;
    return resourceDisplayValue(resources,ResourceKind::SystemPower,0,"estimated_consumption");
}
// SETs has one battery status row on every model. Unknown devices still use
// real OS telemetry; absence of an EC charge setter must not hide the battery.
inline std::string batteryPowerStatus(const BatteryReading& b,const ResourcePresentationView* resources){
    if(b.present==false)return "Батарея не установлена";
    if(!b.present&&!b.percent&&!b.volts&&!b.watts)return "Питание: нет данных батареи";
    std::string text=!b.onlineKnown?"Питание: источник неизвестен":b.online?"Питание: от сети":"Питание: от батареи";
    const auto percent=resourceDisplayValue(resources,ResourceKind::Battery,0,"charge");
    const auto celsius=resourceDisplayValue(resources,ResourceKind::Battery,0,"temperature");
    if(percent)text+=" · заряд "+monitoringMetric(percent)+" %";
    if(celsius)text+=" · "+monitoringMetric(celsius,1)+" °C";
    if(b.dynamicStale)text+=" · последние показания";
    return text;
}
inline BatteryCard batteryCard(const BatteryReading& b,const ResourcePresentationView* resources) {
    const auto value=[&](std::string_view metric){
        return resourceDisplayValue(resources,ResourceKind::Battery,0,metric);
    };
    const auto percent=value("charge"),watts=value("power"),temperature=value("temperature"),
        voltage=value("voltage"),current=value("current");
    const std::string flow = b.present == false ? "Нет батареи" :
        b.flow == BatteryFlow::Charging ? "Зарядка" :
        b.flow == BatteryFlow::Discharging ? "Разряд" :
        b.flow == BatteryFlow::Idle ? (b.onlineKnown ? (b.online ? "От сети" : "От батареи") : "Источник неизвестен") : "Нет показаний";
    BatteryCard card;
    const auto cycles=resourceDisplayUnsigned(resources,ResourceKind::Battery,0,"cycles");
    card.left = {{{"Батарея", monitoringMetric(percent) + " %"},
                  {flow, monitoringMetric(watts ? std::optional<double>(std::abs(*watts)) : std::nullopt, 1) + " Вт"},
                  {"Циклов", cycles ? std::to_string(*cycles) : "—"}}};
    card.right = {monitoringMetric(temperature, 1) + " °C", monitoringMetric(voltage, 2) + " В",
        monitoringGrouped(current ? std::optional<double>(std::abs(*current)) : std::nullopt) + " мА"};
    card.flow = b.flow;
    return card;
}
inline ProcessorCard processorCard(const MonitoringSnapshot& sample, const FanReadout& fan, int channel,
                                  const std::string& fanSource,const std::string& displayDevice,const ResourcePresentationView* resources) {
    if(channel<0||channel>1)throw std::out_of_range("Processor presentation channel");
    if(channel==1)return gpu::loadCard(sample,fan,fanSource,displayDevice,resources);
    const auto& p = sample.processors[channel];
    const auto reading=[&](std::string_view metric){return resources?resources->measurement(ResourceKind::Processor,0,metric):nullptr;};
    const auto* temperature=reading("temperature");const auto* load=reading("usage");
    const auto* clock=reading("frequency");const auto* power=reading("power");
    const auto* corePower=reading("core_power");const auto* soc=reading("soc_temperature");const auto* apu=reading("apu_package_power");
    const auto temperatureValue=resourceDisplayValue(resources,ResourceKind::Processor,0,"temperature");
    const auto usageValue=resourceDisplayValue(resources,ResourceKind::Processor,0,"usage");
    const auto clockValue=resourceDisplayValue(resources,ResourceKind::Processor,0,"frequency");
    const auto powerValue=resourceDisplayValue(resources,ResourceKind::Processor,0,"power");
    const auto& temperatureSource=temperature?temperature->source:p.temperatureSource;
    const auto& usageSource=load?load->source:p.usageSource;
    const auto& clockSource=clock?clock->source:p.clockSource;
    const auto& powerSource=power?power->source:p.powerSource;
    const bool intel=sample.processorVendors[0]==ProcessorVendor::Intel;
    const auto source = [](const std::string& text) { return text.empty() ? std::string("нет данных") : text; };
    ProcessorCard card;
    card.title = channel == 0 ? "CPU" : "GPU";
    if (!sample.processorModels[channel].empty()) card.title += " · " + sample.processorModels[channel];
    card.values = {monitoringGrouped(fan.rpmAvailable ? std::optional<double>(fan.rpm) : std::nullopt),
        monitoringMetric(fan.pwmAvailable ? std::optional<double>(fan.dutyPercent) : std::nullopt),
        monitoringMetric(temperatureValue, 1, true)};
    card.sources = {source(fanSource), source(fanSource),
        temperatureSource.empty() ? "Источник недоступен" : temperatureSource};
    card.summary = {{{"Загрузка ", monitoringMetric(usageValue, 1, true) + "%"},
                     {"", monitoringGrouped(clockValue) + " МГц"},
        {channel == 0 ? (sample.processorVendors[0] != ProcessorVendor::Amd || powerSource == "PawnIO / CPU core energy" ? "CPU " : "APU ") : "GPU ",
            monitoringMetric(intel?resourceDisplayValue(resources,ResourceKind::Processor,0,"core_power"):powerValue, 2) + " Вт"}}};
    for (const auto& metric : card.summary) {
        if (!card.information[0].empty()) card.information[0] += " · ";
        card.information[0] += metric.label + metric.value;
    }
    card.information[1] = channel == 0 ? "Загрузка: " + source(usageSource) + " · Частота: " + source(clockSource) :
        "Загрузка / частота: " + source(usageSource);
    card.information[2] = "Мощность: " + source(intel?(corePower?corePower->source:sample.cpuCorePowerSource):powerSource);
    if (channel==0 && sample.processorVendors[0]==ProcessorVendor::Intel) {
        // Same Wujie two-measurement/source slots. TjMax is CPU metadata in
        // diagnostics, never a third measured row disguised as a source.
        card.secondaryTitle="PCH";
        card.secondaryValue=monitoringMetric(resourceDisplayValue(resources,ResourceKind::Chipset,0,"temperature"));
        card.secondaryUnit="°C";
        card.secondaryExtraTitle="Package";
        card.secondaryExtraValue=monitoringMetric(powerValue,2);
        card.secondaryExtraUnit="Вт";
        card.secondarySource="Intel · PawnIO";
        return card;
    }
    if (sample.processorVendors[0]!=ProcessorVendor::Amd && sample.processorVendors[channel]!=ProcessorVendor::Amd) {
        card.secondaryTitle=channel==0?"CPU":"GPU";
        card.secondaryValue="—";card.secondarySource="Нет дополнительных данных";
        return card;
    }
    card.secondaryTitle = channel == 0 ? "SoC" : "NPU";
    card.secondaryValue = monitoringMetric(channel == 0 ? resourceDisplayValue(resources,ResourceKind::Processor,0,"soc_temperature") : sample.npuUsagePercent, channel == 0 ? 1 : 0, true);
    card.secondaryUnit = channel == 0 ? "°C" : "%";
    if (channel == 0) {
        const auto conciseSource = [](const std::string& text) {
            const auto separator = text.find(" · ");
            if (separator != std::string::npos) return text.substr(0, separator);
            const auto detail = text.find(" / ");
            return detail == std::string::npos ? text : text.substr(0, detail);
        };
        card.secondarySource = conciseSource(source(soc?soc->source:sample.socSource));
        for(std::size_t i=0;i<sample.additionalSensors.size();++i){const auto& sensor=sample.additionalSensors[i];
            if(sensor.device=="HPBSD system"&&sensor.metric=="temperature"&&sensor.value){
                const auto* observed=resources?resources->measurement(ResourceKind::Sensor,static_cast<unsigned>(i),"temperature"):nullptr;
                card.secondaryTitle="System";card.secondaryValue=monitoringMetric(resourceDisplayValue(resources,ResourceKind::Sensor,static_cast<unsigned>(i),"temperature"),1,true);
                card.secondarySource=observed?observed->source:sensor.source;break;
            }}
        card.secondaryExtraTitle = "APU";
        card.secondaryExtraValue = monitoringMetric(resourceDisplayValue(resources,ResourceKind::Processor,0,"apu_package_power"), 2);
        card.secondaryExtraUnit = "Вт";
        card.secondarySource += " · " + conciseSource(source(apu?apu->source:sample.apuPackagePowerSource));
    } else {
        card.secondarySource = monitoringGrouped(sample.npuClockMhz) + " МГц · " +
            monitoringMetric(sample.npuPowerWatts,2) + " Вт\n" +
            source(sample.npuClockSource) + " · " + source(sample.npuPowerSource);
    }
    return card;
}
} // namespace mc::gui::ui
