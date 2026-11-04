#pragma once
#include "Geometry.h"
#include "MagicControl/GpuDevice.h"
#include "DeviceHeader.h"
#include "DisplayFormat.h"
#include <array>
#include <cmath>
#include <format>
#include <string>
namespace mc::gui::ui {
inline constexpr int kGpuCardHeight=342;
inline std::vector<GpuReading> nvidiaPreviewCard(){
    GpuReading r;const int index=selectedLaptopIndex();
    if(index<0||index>=static_cast<int>(kSupportedLaptops.size())||!selectedNvidiaPreviewModel()){
        r.name="GPU · Просмотр";r.status="Профиль GPU не определён · показания не читаются, управление отключено.";return {std::move(r)};
    }
    const auto& model=kSupportedLaptops[index];r.name=std::string(model.previewGpu)+" · Просмотр";
    r.status=std::string("Профиль ")+std::string(model.name)+" · статическая карточка. Показания не читаются, управление отключено.";
    return {std::move(r)};
}
struct GpuCardGeometry {Rect card,title;std::array<Rect,4> metrics;std::array<Rect,5> details;Rect powerLabel,edit,spin,apply,reset,status;};
inline GpuCardGeometry gpuCardGeometry(int width,int index,Scale s={}){
    GpuCardGeometry g;const int pad=s(12),left=pad+s(10),right=width-pad-s(10);
    const int top=pad+index*s(kGpuCardHeight+6),w=right-left;
    g.card={pad,top,width-pad,top+s(kGpuCardHeight)};g.title={left,top+s(8),right,top+s(38)};
    for(int i=0;i<4;++i)g.metrics[i]={left+i*w/4,top+s(44),left+(i+1)*w/4,top+s(100)};
    for(int i=0;i<5;++i)g.details[i]={left,top+s(109+i*23),right,top+s(132+i*23)};
    g.powerLabel={left,top+s(240),left+s(150),top+s(270)};
    g.edit={left+s(155),top+s(240),left+s(219),top+s(270)};
    g.spin={g.edit.right,g.edit.top,g.edit.right+s(14),g.edit.bottom};
    g.apply={g.spin.right+s(8),g.edit.top,g.spin.right+s(108),g.edit.bottom};
    g.reset={g.apply.right+s(8),g.edit.top,g.apply.right+s(158),g.edit.bottom};
    g.status={left,top+s(282),right,top+s(334)};return g;
}
inline std::string gpuValue(std::optional<unsigned> value,const char* suffix){return value?std::format("{} {}",*value,suffix):std::string("— ")+suffix;}
inline std::string gpuWatts(std::optional<unsigned> value){return value?displayNumber(*value/1000.0,1)+" Вт":"— Вт";}
inline std::string gpuBytes(std::optional<std::uint64_t> value){return value?displayNumber(*value/1073741824.0,2)+" ГиБ":"— ГиБ";}
inline std::array<std::string,4> gpuMetrics(const GpuReading& r){return {
    gpuValue(r.temperature,"°C"),gpuValue(r.utilization,"%"),gpuWatts(r.powerMw),gpuBytes(r.usedBytes)};}
inline constexpr std::array<const char*,4> gpuMetricCaptions{"Температура","Загрузка GPU","Потребление","Занято VRAM"};
inline const char* gpuPowerCaption(const GpuReading& r){return r.powerUnit==GpuPowerUnit::Percent?"Лимит мощности, %":"Лимит мощности, Вт";}
inline std::array<std::string,5> gpuDetails(const GpuReading& r){
    if(r.powerUnit==GpuPowerUnit::Percent||r.source.starts_with("AMD")||r.source.starts_with("Linux · amdgpu")){
        const auto percentage=[&](std::optional<int> v){const auto* unit=r.powerUnit==GpuPowerUnit::Percent?" %":" Вт";return v?std::to_string(*v)+unit:std::string("—")+unit;};
        return {"Частота GPU: "+gpuValue(r.graphicsMhz,"МГц")+" · память: "+gpuValue(r.memoryMhz,"МГц"),
            "VRAM: всего "+gpuBytes(r.totalBytes)+" · свободно "+gpuBytes(r.freeBytes)+" · "+r.memoryType,
            "Вентилятор: "+gpuValue(r.fanRpm,"об/мин")+" · Hotspot: "+(r.hotspotCelsius?displayNumber(*r.hotspotCelsius,1)+" °C":"— °C"),
            "Power Limit: "+percentage(r.powerValue)+" · диапазон: "+percentage(r.powerMinimum)+" … "+percentage(r.powerMaximum)+" · штатный: "+percentage(r.powerDefault),
            "Драйвер: "+(r.driver.empty()?"—":r.driver)+" · VBIOS: "+(r.vbios.empty()?"—":r.vbios)+" · "+r.source};
    }
    return {"Частота GPU: "+gpuValue(r.graphicsMhz,"МГц")+" · память: "+gpuValue(r.memoryMhz,"МГц")+" · "+(r.pstate?"P"+std::to_string(*r.pstate):"P—"),
        "VRAM: всего "+gpuBytes(r.totalBytes)+" · свободно "+gpuBytes(r.freeBytes)+
            (r.hotspotCelsius?" · Hot Spot "+displayNumber(*r.hotspotCelsius,1)+" °C":"")+
            (r.memoryJunctionCelsius?" · Junction "+displayNumber(*r.memoryJunctionCelsius,1)+" °C":r.memoryCelsius?" · память "+displayNumber(*r.memoryCelsius,1)+" °C":""),
        "Power Limit: "+gpuWatts(r.limitMw)+" · действующий предел: "+gpuWatts(r.enforcedMw),
        "Диапазон: "+gpuWatts(r.minimumMw)+" – "+gpuWatts(r.maximumMw)+" · штатный: "+gpuWatts(r.defaultMw),
        "Драйвер: "+(r.driver.empty()?"—":r.driver)+" · VBIOS: "+(r.vbios.empty()?"—":r.vbios)};
}
}
