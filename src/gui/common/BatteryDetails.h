#pragma once
#include "MonitoringCards.h"
#include "BatteryPolicy.h"
#include "Theme.h"
#include "SettingsCardContract.h"
#include "../../protocols/uniwill/gm6ir0c/BatteryDecode.h"
#include <span>
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/BatteryIdentityHistory.h"
#include <vector>
#include <cstdio>
#include <string_view>

namespace mc::gui::ui {
inline std::string batteryDateAndAge(const std::string& value){
    if(value.empty())return "—";
    const auto age=batteryAgeMonthsLocal(value);
    return age?value+" · "+std::to_string(*age)+" "+translate("мес."):value;
}
inline std::string batteryHex(std::optional<std::uint16_t> v){
    if(!v)return "—";
    char s[12]{};std::snprintf(s,sizeof(s),"0x%04X",unsigned(*v));return s;
}
inline std::string batteryRawByte(std::optional<std::uint8_t> v){
    if(!v)return "—";
    char s[10]{};std::snprintf(s,sizeof(s),"0x%02X",unsigned(*v));return s;
}
inline std::string batteryHex32(std::optional<std::uint32_t> v){if(!v)return "—";char s[14]{};std::snprintf(s,sizeof(s),"0x%08X",unsigned(*v));return s;}
inline std::string ecBatteryStatus(const EcBatteryReading& e){
    return !e.supported?"EC-телеметрия этой модели недоступна":e.status.empty()?"Нет показаний EC":e.status;
}
inline std::vector<InlineMetric> ecBatteryDetails(const EcBatteryReading& e){
    const auto& b=e.battery;
    const auto capacity=[](std::optional<std::uint16_t> v){return v?std::to_string(*v)+" "+translate("мА·ч"):"—";};
    const auto low=[](std::optional<std::uint8_t> v){const auto n=v?(*v&0x7F):0;return v&&n>=1&&(n<=95||n==97)?std::to_string(n)+" % · "+batteryRawByte(v):"— · "+batteryRawByte(v);};
    const auto state=b.flow==BatteryFlow::Charging?"Зарядка":b.flow==BatteryFlow::Discharging?"Разряд":b.flow==BatteryFlow::Idle?"Покой":"—";
    const auto ratio=e.fullMah&&*e.fullMah&&e.remainingMah?std::optional<double>(100.* *e.remainingMah / *e.fullMah):std::nullopt;
    const auto upper=e.upperRaw&&(*e.upperRaw&0x7F)<=100?std::to_string((*e.upperRaw&0x7F)?(*e.upperRaw&0x7F):100)+" %":"—";
    const auto rating=[](std::optional<std::uint8_t> v){return v&&*v&&*v!=255?std::to_string(*v)+" "+translate("Вт"):"—";};
    std::vector<InlineMetric> rows{
        {"Заряд SOC",monitoringMetric(b.percent)+" %"},
        {"Заряд по ёмкостям",monitoringMetric(ratio,1)+" %"},
        {"Состояние",state},
        {"Мощность (U × I)",monitoringMetric(b.watts,2)+" "+translate("Вт")},
        {"Ток батареи",monitoringMetric(b.milliamps)+" "+translate("мА")},
        {"Напряжение батареи",monitoringMetric(b.volts,3)+" "+translate("В")},
        {"Температура батареи",monitoringMetric(b.celsius,1)+" °C"},
        {"Циклов",b.cycles?std::to_string(*b.cycles):"—"},
        {"Остаточная ёмкость",capacity(e.remainingMah)},
        {"Полная ёмкость BMS",capacity(e.fullMah)},
        {"Заводская ёмкость",capacity(e.designMah)},
        {"Здоровье по ёмкости EC",monitoringMetric(b.healthPercent,3)+" %"},
        {"Износ по ёмкости EC",monitoringMetric(b.healthPercent?std::optional<double>(std::max(0.,100.-*b.healthPercent)):std::nullopt,3)+" %"},
        {"Порог остановки",upper+" · "+batteryRawByte(e.upperRaw)},
        {"Порог 0x7D0 (назначение?)",low(e.restartRaw)},
        {"Флаг достигнутого порога",e.upperRaw?(*e.upperRaw&0x80?"Установлен":"Снят"):"—"},
        {"Платформа EC (0x7C3)",batteryRawByte(e.platformRaw)},
        {"Статус батареи (0x432)",batteryHex(e.state)},
        {"Готовность EC (0x490)",batteryRawByte(e.readyRaw)},
        {"Флаги 0x742 / 0x7A6",batteryRawByte(e.gateRaw)+" / "+batteryRawByte(e.voltagePolicyRaw)},
        {"Адаптер EC (0x7CC)",batteryRawByte(e.chargerRaw)},
        {"Рейтинг Type-C A / B",rating(e.typeCRatingA)+" / "+rating(e.typeCRatingB)}};
    if(gmRamFanProfile(e.profileId)){
        rows.resize(13); // Common readings/capacities; board-specific policy below.
        const bool pair=e.upperRaw&&e.restartRaw;
        const bool disabled=pair&&!(*e.upperRaw&0x7F)&&!(*e.restartRaw&0x7F);
        const auto bit=[](std::optional<std::uint8_t> v,unsigned mask){return !v?std::string("—"):*v&mask?std::string("Установлен"):std::string("Снят");};
        const auto policy=!e.voltagePolicyRaw?std::string("—"):
            (*e.voltagePolicyRaw&0x30)==0?std::string("Производительность"):
            (*e.voltagePolicyRaw&0x30)==0x10?std::string("Сбалансированный"):
            (*e.voltagePolicyRaw&0x30)==0x20?std::string("Защита здоровья"):std::string("Неизвестно");
        rows.push_back({"Внешнее питание · XCIN",b.onlineKnown?(b.online?"Подключено":"Отключено"):"—"});
        rows.push_back({"Батарея установлена · BPST",bit(e.readyRaw,2)});
        rows.push_back({"Данные батареи готовы · BSOK",bit(e.readyRaw,4)});
        rows.push_back({"Статус батареи (0x432)",batteryHex(e.state)});
        rows.push_back({"ISDB (0x497)",batteryRawByte(e.isdbRaw)});
        const auto alert=std::find_if(e.registers.begin(),e.registers.end(),[](const auto& f){return f.address==0x494;});
        rows.push_back({"Предупреждение OEM (0x494)",alert!=e.registers.end()&&alert->value?(*alert->value?"Есть · "+batteryHex(alert->value):"Нет"):"—"});
        rows.push_back({"Верхний порог · настройка",disabled?"Не задан · 0/0":upper+" · "+batteryRawByte(e.upperRaw)});
        if(gmAmdProfile(e.profileId)){
            const auto dbd=std::find_if(e.registers.begin(),e.registers.end(),[](const auto& f){return f.address==0x7D0;});
            rows.push_back({"DBD1 (0x7D0) · не порог зарядки",dbd!=e.registers.end()?batteryHex(dbd->value):"—"});
        }else rows.push_back({"Нижний порог · роль не подтверждена",low(e.restartRaw)});
        rows.push_back({"Защита BatteryProtection2",policy+" · "+batteryRawByte(e.voltagePolicyRaw)});
        rows.push_back({"Подстановка полной ёмкости в Windows",e.isdbRaw&&b.cycles?((*e.isdbRaw&1)&&*b.cycles<50?"Да · ISDB, циклов < 50":"Нет"):"—"});
    }
    return rows;

}
struct BatteryDetailRow{std::string label,value,source;};
inline ColorRef primaryBatteryValueColor(std::size_t row,std::string_view value){
    if(value=="—")return theme::kTextMuted;
    if(value.starts_with("≈ ")){
        if(row==2)return theme::kBatteryDischarging;
        if(row==3||row==4)return theme::kBatteryCharging;
    }
    return theme::kTextPrimary;
}
inline std::array<InlineMetric,6> batteryCurrentMetrics(const BatteryReading& b,bool lenovoEc=false){
    const auto flow=b.flow==BatteryFlow::Charging?"Зарядка":b.flow==BatteryFlow::Discharging?"Разряд":"Мощность батареи";
    std::array<InlineMetric,6> rows{{{translate("Заряд"),b.present==false?translate("Нет батареи"):monitoringMetric(b.percent)+" %"},
             {translate(flow),monitoringMetric(b.watts?std::optional<double>(std::abs(*b.watts)):std::nullopt,1)+" "+translate("Вт")},
             {translate("Циклы"),b.cycles?std::to_string(*b.cycles):"—"},
             {translate("Температура"),monitoringMetric(b.celsius,1)+" °C"},
             {translate("Напряжение"),monitoringMetric(b.volts,2)+" "+translate("В")},
             {translate("Ток"),monitoringGrouped(b.milliamps?std::optional<double>(std::abs(*b.milliamps)):std::nullopt)+" "+translate("мА")}}};
    if(lenovoEc||b.deviceId=="EC/Lenovo-KWCN/BAT0"){
        const auto status=b.flow==BatteryFlow::Charging?"Зарядка":b.flow==BatteryFlow::Discharging?"Разряд":b.flow==BatteryFlow::Idle?"Покой":"Неизвестно";
        const auto direction=!b.milliamps?"Неизвестно":*b.milliamps<0?"Разряд":*b.milliamps>0?"Зарядка":"Нулевой ток";
        rows[1].label=translate("Статус EC");rows[1].value=translate(status)+" · "+rows[1].value;
        rows[5].label=translate("Направление тока");rows[5].value=translate(direction)+" · "+rows[5].value;
    }
    return rows;
}
inline ColorRef batteryCurrentColor(const BatteryReading& b,std::size_t row){
    if(row==1&&b.watts){
        if(b.flow==BatteryFlow::Charging)return theme::kBatteryCharging;
        if(b.flow==BatteryFlow::Discharging)return theme::kBatteryDischarging;
    }
    return theme::kTextPrimary;
}
inline std::string batteryDuration(std::optional<double> seconds){
    if(!seconds||!std::isfinite(*seconds)||*seconds<0||*seconds>UINT32_MAX)return "—";
    const auto minutes=static_cast<unsigned long long>(std::llround(*seconds/60.));
    char value[40]{};std::snprintf(value,sizeof(value),"≈ %02llu:%02llu",minutes/60,minutes%60);return std::string(value)+" "+translate("ч");
}
inline std::optional<double> batteryRemainingSeconds(const BatteryReading& b,bool charging){
    if((charging&&b.flow!=BatteryFlow::Charging)||(!charging&&b.flow!=BatteryFlow::Discharging))return {};
    const auto native=charging?b.estimatedChargeSeconds:b.estimatedDischargeSeconds;
    if(native)return *native;
    if(b.capacityRelative||!b.remainingCapacity)return {};
    const auto left=charging?(b.fullCapacity?std::optional<double>(*b.fullCapacity-*b.remainingCapacity):std::nullopt):b.remainingCapacity;
    if(!left||*left<0)return {};
    if(b.capacityUnit=="mAh"&&b.milliamps&&std::abs(*b.milliamps)>0)return *left/std::abs(*b.milliamps)*3600.;
    if(b.capacityUnit=="mWh"&&b.watts&&std::abs(*b.watts)>0)return *left/(std::abs(*b.watts)*1000.)*3600.;
    return {};
}
// Estimates use only coherent, fresh EC capacity/current, never OS fallback.
inline std::array<BatteryDetailRow,3> ecBatteryTimes(const EcBatteryReading& ec){
    if((!ec.coherent||ec.battery.dynamicStale)&&!ec.retainedForDisplay)return {{{"До разряда (оценка, ч:мин)","—","EC"},{"До 100% (оценка, ч:мин)","—","EC"},{"До порога (оценка, ч:мин)","—","EC"}}};
    if(ec.profileId=="lenovo-legion-kwcn"){
        const auto& b=ec.battery;
        const bool contradictory=(b.flow==BatteryFlow::Charging&&((b.milliamps&&*b.milliamps<0)||(b.watts&&*b.watts<0)))||
            (b.flow==BatteryFlow::Discharging&&((b.milliamps&&*b.milliamps>0)||(b.watts&&*b.watts>0)));
        if(contradictory)return {{{"До разряда (оценка, ч:мин)","—","EC"},{"До 100% (оценка, ч:мин)","—","EC"},{"До порога (оценка, ч:мин)","—","EC"}}};
        return {{{"До разряда (оценка, ч:мин)",batteryDuration(batteryRemainingSeconds(ec.battery,false)),"EC"},
        {"До 100% (оценка, ч:мин)",batteryDuration(batteryRemainingSeconds(ec.battery,true)),"EC"},{"До порога (оценка, ч:мин)","—","EC"}}};
    }
    const auto ecSeconds=[&](bool charging)->std::optional<double>{
        if((charging&&ec.battery.flow!=BatteryFlow::Charging)||(!charging&&ec.battery.flow!=BatteryFlow::Discharging)||!ec.remainingMah||!ec.battery.milliamps||*ec.battery.milliamps<=0)return {};
        if(charging&&(!ec.fullMah||*ec.remainingMah>*ec.fullMah))return {};
        const auto remaining=charging?double(*ec.fullMah-*ec.remainingMah):double(*ec.remainingMah);
        return remaining/ *ec.battery.milliamps*3600.;
    };
    std::string toLimit="—";
    if(!(gmRamFanProfile(ec.profileId)&&ec.upperRaw&&ec.restartRaw&&!(*ec.upperRaw&0x7F)&&!(*ec.restartRaw&0x7F))&&ec.upperRaw&&(*ec.upperRaw&0x7F)<=100&&ec.fullMah&&ec.remainingMah){
        const auto percent=(*ec.upperRaw&0x7F)?(*ec.upperRaw&0x7F):100;
        const double target=*ec.fullMah*percent/100.;
        if(*ec.remainingMah>=target)toLimit=translate("Заряд не ниже порога");
        else if(ec.battery.flow==BatteryFlow::Charging&&ec.battery.milliamps&&*ec.battery.milliamps>0)
            toLimit=batteryDuration((target-*ec.remainingMah)/ *ec.battery.milliamps*3600.);
    }
    return {{{"До разряда (оценка, ч:мин)",batteryDuration(ecSeconds(false)),"EC"},
             {"До 100% (оценка, ч:мин)",batteryDuration(ecSeconds(true)),"EC"},
             {"До порога (оценка, ч:мин)",toLimit,"EC"}}};
}
// One primary card for every profile/OS. Each source keeps its own units;
// Windows base-plan settings are never represented as measured CPU telemetry.
inline std::string primaryBatteryOsSource(const BatteryReading& os){
    return os.source.starts_with("Windows")?"Windows":os.source.starts_with("Linux")?"Linux":"OS";
}
inline std::vector<BatteryDetailRow> primaryBatteryDetails(const BatteryReading& os,const EcBatteryReading& ec,const BatteryPowerPolicy& policy,std::optional<int> targetVoltageMv={}){
    const auto times=ecBatteryTimes(ec);
    std::vector<BatteryDetailRow> rows;
    const auto source=primaryBatteryOsSource(os);
    std::string native="—";
    if(os.fullCapacity&&os.designCapacity&&!os.capacityUnit.empty()){
        const bool wattHours=!os.capacityRelative&&os.capacityUnit=="mWh";
        const double divisor=wattHours?1000.:1.;
        const auto unit=translate(os.capacityRelative?"Относительные единицы":os.capacityUnit=="mAh"?"мА·ч":wattHours?"Вт·ч":"мВт·ч");
        native=monitoringMetric(std::optional<double>(*os.fullCapacity/divisor),wattHours?2:0)+" / "+
            monitoringMetric(std::optional<double>(*os.designCapacity/divisor),wattHours?2:0)+" "+unit;
    }
    const bool valid=(ec.coherent&&!ec.battery.dynamicStale)||ec.retainedForDisplay;
    const bool lenovo=ec.profileId=="lenovo-legion-kwcn";
    const auto health=valid&&ec.fullMah&&ec.designMah&&*ec.designMah>0?
        std::optional<double>(100.* *ec.fullMah/ *ec.designMah):valid&&lenovo?ec.battery.healthPercent:std::nullopt;
    std::string ecCapacity=valid&&ec.fullMah&&ec.designMah?std::to_string(*ec.fullMah)+" / "+std::to_string(*ec.designMah)+" "+translate("мА·ч"):"—";
    if(valid&&lenovo&&ec.battery.fullCapacity&&ec.battery.designCapacity&&!ec.battery.capacityUnit.empty()){
        const bool wh=ec.battery.capacityUnit=="mWh";const auto divisor=wh?1000.:1.;
        ecCapacity=monitoringMetric(std::optional<double>(*ec.battery.fullCapacity/divisor),wh?2:0)+" / "+
            monitoringMetric(std::optional<double>(*ec.battery.designCapacity/divisor),wh?2:0)+" "+translate(wh?"Вт·ч":"мА·ч");}
    rows.push_back({"Полная / заводская ёмкость",ecCapacity,"EC"});
    auto healthText=monitoringMetric(health,1)+" %";
    if(const auto wattHours=estimatedEcFullWattHours(ec,targetVoltageMv))
        healthText+=" · ≈ "+monitoringMetric(wattHours,1)+" "+translate("Вт·ч");
    rows.push_back({"Здоровье",std::move(healthText),"EC"});
    // Matching fields occupy matching rows in both source cards.
    rows.push_back({"До разряда",times[0].value,"EC"});
    rows.push_back({"До полной зарядки",times[1].value,"EC"});
    std::string chargeMode="—";
    if(valid&&lenovo&&ec.conservationMode&&ec.rapidChargeMode){
        chargeMode=*ec.conservationMode?(*ec.rapidChargeMode?translate("Конфликт режимов"):"Conservation"):(*ec.rapidChargeMode?"Rapid":"Normal");}
    rows.push_back({lenovo?"Режим зарядки":"До лимита зарядки",lenovo?chargeMode:times[2].value,"EC"});
    rows.push_back({"Дата изготовления",batteryDateAndAge(ec.battery.manufactured),"EC"});
    const auto request=valid&&lenovo?(monitoringMetric(ec.requestedChargeMilliamps)+" "+translate("мА")+" / "+monitoringMetric(ec.requestedChargeVolts,2)+" "+translate("В")):"—";
    rows.push_back({lenovo?"Запрос BMS: ток / напряжение":"Термочасы",lenovo?request:valid&&ec.aging.realHistory?std::to_string(*ec.aging.realHistory):"—","EC"});
    const auto frequency=[](std::optional<std::uint32_t> value){return !value?std::string("—"):*value?std::to_string(*value)+" "+translate("МГц"):translate("Не задан");};
    const auto number=[](std::optional<std::uint32_t> value){return value?std::to_string(*value):std::string("—");};
    rows.push_back({"Полная / заводская ёмкость",native,source});
    rows.push_back({"Здоровье",monitoringMetric(os.healthPercent,1)+" %",source});
    rows.push_back({"План питания",policy.schemeName.empty()?"—":policy.schemeName,"Windows"});
    rows.push_back({"Предел частоты: БП / батарея",frequency(policy.frequencyMHz.ac)+" / "+frequency(policy.frequencyMHz.dc),"Windows"});
    rows.push_back({"Приоритет экономии: БП / батарея",number(policy.epp.ac)+" / "+number(policy.epp.dc),"Windows"});
    rows.push_back({"Дата изготовления",batteryDateAndAge(os.manufactured),source});
    rows.push_back({"","",source});
    return rows;
}
// Shared hint content/regions; native adapters only show their platform tooltip.
inline std::vector<TooltipRegion> batteryPrimaryTooltips(const BatteryPrimaryRowsGeometry& geometry,bool lenovo=false){
    const std::array<const char*,3> hints{{
        "Базовый план питания Windows. В парах значений слева указан БП, справа — батарея.",
        "Слева — БП, справа — батарея. Это заданный в базовом плане Windows предел частоты CPU в МГц, а не текущая частота. «Не задан» означает отсутствие этого ограничения в плане.",
        "Слева — БП, справа — батарея. EPP: 0 — приоритет производительности, 100 — приоритет экономии. Это настройка базового плана Windows, а не процент сэкономленной энергии."
    }};
    std::vector<TooltipRegion> result;
    for(std::size_t i=0;i<hints.size();++i){
        const auto row=9+i;auto bounds=geometry.details.labels[row];
        bounds.right=geometry.details.values[row].right;
        result.push_back({bounds,translate(hints[i])});
    }
    auto exposure=geometry.details.labels[6];exposure.right=geometry.details.values[6].right;
    result.push_back({exposure,translate(lenovo?"Запрос BMS — это запрошенные ток и напряжение зарядки, а не измеренные значения на батарее.":
        "Термочасы: сохранённая история плюс новое накопление EC. Счётчик учитывает температуру и высокое напряжение.")});
    return result;
}
inline std::vector<BatteryDetailRow> batteryAllDetails(const BatteryReading& os,const EcBatteryReading& ec){
    const auto text=[](const std::string& v){return v.empty()?std::string("—"):v;};
    const auto number=[](std::optional<std::uint32_t> v){return v?std::to_string(*v):std::string("—");};
    const auto capacity=[&](std::optional<double> v){return monitoringMetric(v)+" "+translate(os.capacityRelative?"Относительные единицы":os.capacityUnit=="mAh"?"мА·ч":"мВт·ч");};
    const auto flow=os.flow==BatteryFlow::Charging?"Зарядка":os.flow==BatteryFlow::Discharging?"Разряд":os.flow==BatteryFlow::Idle?"Покой":"—";
    const std::string source=os.source.starts_with("Windows")?"Windows":"OS";
    std::vector<BatteryDetailRow> rows{
        {"Модель батареи",text(os.model),source},
        {"Производитель батареи",text(os.manufacturer),source},
        {"Серийный номер",text(os.serial),source},
        {"Химия батареи",text(os.chemistry),source},
        {"Дата изготовления",text(os.manufactured),source},
        {"Идентификатор батареи",text(os.uniqueId),source},
        {"Заряд SOC",monitoringMetric(os.percent)+" %",source},
        {"Состояние",flow,source},
        {"Внешнее питание",os.onlineKnown?(os.online?"Подключено":"Отключено"):"—",source},
        {"Мощность батареи",monitoringMetric(os.watts,2)+" "+translate("Вт"),source},
        {"Напряжение батареи",monitoringMetric(os.volts,3)+" "+translate("В"),source},
        {source=="Windows"?"Ток (расчёт W / V)":"Ток батареи",monitoringMetric(os.milliamps)+" "+translate("мА"),source},
        {"Температура батареи",monitoringMetric(os.celsius,1)+" °C",source},
        {"Циклов",os.cycles?std::to_string(*os.cycles):"—",source},
        {"Проектная ёмкость",capacity(os.designCapacity),source},
        {"Полная ёмкость",capacity(os.fullCapacity),source},
        {"Остаточная ёмкость",capacity(os.remainingCapacity),source},
        {"Здоровье по ёмкости",monitoringMetric(os.healthPercent,3)+" %",source},
        {"Возможности драйвера",batteryHex32(os.capabilities),source},
        {"Технология батареи",number(os.technology),source},
        {"Первое предупреждение",number(os.alert1),source},
        {"Второе предупреждение",number(os.alert2),source},
        {"Критический резерв",number(os.criticalBias),source},
        {"Флаги Windows",batteryHex32(os.powerState),source},
        {"Критический флаг драйвера",os.powerState?((*os.powerState&8)?"Установлен":"Снят"):"—",source}};
    for(auto& item:ecBatteryDetails(ec))rows.push_back({std::move(item.label),std::move(item.value),"EC"});
    const auto flag=[&](unsigned mask){return ec.state?(*ec.state&mask?"Установлен":"Снят"):"—";};
    rows.push_back({"Флаг разряда (0x432.0)",flag(1),"EC"});
    rows.push_back({"Флаг зарядки (0x432.1)",flag(2),"EC"});
    rows.push_back({"Критический заряд (0x432.2)",flag(4),"EC"});
    rows.push_back({"ISDB (0x497.0)",ec.isdbRaw?(*ec.isdbRaw&1?"Установлен":"Снят"):"—","EC"});
    rows.push_back({"До разряда (оценка, ч:мин)",batteryDuration(batteryRemainingSeconds(os,false)),source});
    rows.push_back({"До 100% (оценка, ч:мин)",batteryDuration(batteryRemainingSeconds(os,true)),source});
    for(auto& row:ecBatteryTimes(ec))rows.push_back(std::move(row));
    // Current signed PawnIO adapter exposes SPD/CPU/SMU only. Do not open or
    // probe a battery SMBus address and do not relabel OS/EC values as PawnIO.
    rows.push_back({"Батарея через PawnIO","Не подключено","PawnIO"});
    const auto find=[&](std::uint16_t address)->const EcBatteryRegisterReading*{
        const auto it=std::find_if(ec.registers.begin(),ec.registers.end(),[&](const auto& value){return value.address==address;});
        return it==ec.registers.end()?nullptr:&*it;
    };
    const bool gm6=gmRamFanProfile(ec.profileId);
    const std::span<const uniwill::BatteryRegisterDefinition> catalogue=gm6?
        std::span<const uniwill::BatteryRegisterDefinition>(mc::gm6::batteryCandidates):std::span<const uniwill::BatteryRegisterDefinition>(uniwill::x5sp4nag::batteryCandidates);
    const std::span<const uniwill::BatteryBitDefinition> bits=gm6?
        std::span<const uniwill::BatteryBitDefinition>(mc::gm6::batteryCandidateBits):std::span<const uniwill::BatteryBitDefinition>(uniwill::x5sp4nag::batteryCandidateBits);
    for(const auto& candidate:catalogue){
        char address[12]{};std::snprintf(address,sizeof(address),"0x%03X",unsigned(candidate.address));
        const auto label=translate(candidate.confirmed?"Поле EC":"Кандидат EC")+" "+address;
        const auto* field=find(candidate.address);std::string value="—";
        if(field&&field->value){
            value=(candidate.width==1?batteryRawByte(static_cast<std::uint8_t>(*field->value)):batteryHex(field->value))+" · "+std::to_string(*field->value);
            if(candidate.width==1){value+=" · ";for(int bit=7;bit>=0;--bit)value+=(*field->value&(1u<<bit))?'1':'0';}
        }else if(field)value+=" · "+translate(field->status=="unstable"?"Нестабильное чтение":field->status=="paused"?"Опрос приостановлен":"Ошибка чтения");
        rows.push_back({label,value,"EC"});
    }
    for(const auto& bit:bits){
        char address[16]{};std::snprintf(address,sizeof(address),"0x%03X.%u",unsigned(bit.address),unsigned(bit.bit));
        const auto* field=find(bit.address);std::string value="—";
        if(field&&field->value)value=(*field->value&(1u<<bit.bit))?"1 · "+translate("Установлен"):"0 · "+translate("Снят");
        rows.push_back({std::string(address)+" "+bit.name,value,"EC"});
    }
    for(auto& row:rows){row.label=translate(row.label);row.value=translate(row.value);}
    return rows;
}
// Source partition, not model-specific pages. PawnIO has no battery provider;
// its unavailable informational row is not relabelled as EC or Windows data.
inline std::array<std::vector<BatteryDetailRow>,2> batteryDetailColumns(const BatteryReading& os,const EcBatteryReading& ec){
    std::array<std::vector<BatteryDetailRow>,2> columns;
    for(auto& row:batteryAllDetails(os,ec)){
        if(row.source=="EC")columns[0].push_back(std::move(row));
        else if(row.source=="Windows"||row.source=="OS"||row.source=="Linux")columns[1].push_back(std::move(row));
    }
    return columns;
}

// Value model consumed by both real renderers and offline adapter tests.
// Construction has no board discovery, GUI, logger or hardware side effects.
struct SettingsCardsModel {
    std::array<int,3> order{settingsCardOrder};
    std::array<bool,3> visible{true,true,true};
    int brightnessMaximum{};
    std::array<std::string,2> sources;
    std::array<BatteryReading,2> current;
    std::vector<BatteryDetailRow> primary;
    bool showDiagnostics{};
    std::array<std::vector<BatteryDetailRow>,2> diagnostics;
};
inline SettingsCardsModel settingsCardsModel(const DeviceProfile& profile,const BatteryReading& os,
        const EcBatteryReading& ec,const BatteryPowerPolicy& policy,bool logging,std::optional<int> targetVoltageMv={}){
    SettingsCardsModel model;
    model.visible[2]=!profileResourceAbsent(profile,"keyboard-lighting");
    model.brightnessMaximum=profile.keyboardBrightnessMaximum;
    auto presentation=ec;
    if(!ec.profileId.empty()&&ec.profileId!=profile.id)presentation={};
    presentation.profileId=profile.id;
    model.visible[0]=batteryCardVisible(profileResourceAbsent(profile,"battery"),os.present,os.dynamicStale,
        presentation.battery.present,presentation.coherent&&!presentation.retainedForDisplay);
    model.sources={"EC",primaryBatteryOsSource(os)};model.current={presentation.battery,os};
    model.primary=primaryBatteryDetails(os,presentation,policy,targetVoltageMv);model.showDiagnostics=model.visible[0]&&logging&&showEcLoggerCard;
    if(model.showDiagnostics)model.diagnostics=batteryDetailColumns(os,presentation);
    return model;
}

}
