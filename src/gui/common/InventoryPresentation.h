#pragma once
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/EcIdentityInventory.h"
#include "Localization.h"
#include <algorithm>
#include <string_view>
#include <utility>

namespace mc::gui::ui {
// INFO is a human hardware report, not a dump of HID descriptors or SCM records.
// Apply this projection to old caches too. Raw inventories and dump collectors
// remain separate, so hiding a diagnostic section never destroys its evidence.
inline bool diagnosticInventorySection(std::string_view title) noexcept {
    return title==mc::kEcIdentityInventoryMetadata || title=="Нативные ACPI/HID интерфейсы и драйверы" ||
           title=="Сеть · звук · камера · NPU" ||
           title=="Подсветка клавиатуры · Linux LED metadata";
}
inline bool diagnosticBatteryRow(const mc::InventoryRow& row) noexcept {
    return row.name=="Идентификатор Windows" || row.name=="Интерфейс Windows" ||
           row.name=="Уникальный идентификатор батареи" || row.name=="Полнота списка батарей" ||
           row.name=="Источник";
}
inline mc::HardwareInventory informationInventory(const mc::HardwareInventory& source,bool uniwillActual) {
    mc::HardwareInventory result;result.readAt=source.readAt;
    for(const auto& section:source.sections){
        if(diagnosticInventorySection(section.title))continue;
        mc::InventorySection visible;visible.title=section.title;
        for(std::size_t index=0;index<section.rows.size();++index){
            const auto& row=section.rows[index];
            if(section.title=="Батарея" && row.name.starts_with("Батарея ") && row.value.starts_with("ACPI\\")){
                // Preserve separate battery groups, using their actual model
                // rather than exposing an ACPI transport path as the name.
                std::string model=mc::kInventoryUnknown;
                for(std::size_t next=index+1;next<section.rows.size();++next){
                    if(section.rows[next].name.starts_with("Батарея "))break;
                    if(section.rows[next].name=="Модель"){model=section.rows[next].value;break;}
                }
                visible.rows.push_back({row.name,std::move(model)});
            }else if(section.title!="Батарея" || !diagnosticBatteryRow(row))visible.rows.push_back(row);
        }
        if(visible.title=="Ноутбук · BIOS · UEFI · EC"){
            if(uniwillActual)mc::ensureInventoryEcIdentityRows(visible);
            else std::erase_if(visible.rows,[](const auto& row){return row.name==mc::kEcProjectCaption||row.name==mc::kEcPlatformCaption||row.name==mc::kEcRomCaption;});
        }
        if(!visible.rows.empty())result.sections.push_back(std::move(visible));
    }
    return result;
}
inline std::string informationReport(const mc::HardwareInventory& source,std::string_view newline,bool uniwillActual) {
    const auto visible=informationInventory(source,uniwillActual);
    const std::string nl(newline);
    std::string result=translate("MagicControl · сведения о ноутбуке")+nl+
        translate("Снимок")+": "+source.readAt+nl;
    for(const auto& section:visible.sections){
        result+=nl+translate(section.title)+nl;
        for(const auto& row:section.rows)result+=translate(row.name)+": "+translate(row.value)+nl;
    }
    return result;
}
} // namespace mc::gui::ui
