#pragma once
#include "MagicControl/ResourceCatalog.h"
#include "DisplayFormat.h"
#include "Localization.h"

namespace mc::gui::ui {
struct DefaultFanGraph {
    const NativeDefaultFanChannel* curve{};
    std::optional<double> temperature,requested,actual;
    std::string legend,source;
    double maximum{100};
    const char* axis{"PWM, %"};
    std::string axisLabel(int percent) const {
        const auto value=maximum*percent/100;
        return curve&&curve->rpm&&percent?displayCompactNumber(value/1000)+"k":displayCompactNumber(value);
    }
};
// Formatting only. All parameters, requests and temperature selection come
// from the accepted core resource; no EC reads, curve math or draft mutation.
inline DefaultFanGraph defaultFanGraph(const ResourcePresentationView& view,unsigned channel,bool enabled){
    DefaultFanGraph out;if(!enabled)return out;
    out.curve=view.defaultFanCurve(channel);if(!out.curve)return out;
    out.temperature=resourceDisplayValue(&view,ResourceKind::Fan,channel,"control_temperature");
    out.requested=resourceDisplayValue(&view,ResourceKind::Fan,channel,out.curve->rpm?"requested_speed":"requested_duty");
    out.actual=resourceDisplayValue(&view,ResourceKind::Fan,channel,out.curve->rpm?"actual_speed":"duty");
    const auto value=[](const std::optional<double>& v){return v?displayCompactNumber(*v):std::string("—");};
    if(out.curve->rpm){
        out.maximum=10000;out.axis="RPM";
        out.legend=formatMessage("{source} {temperature} °C · Запрос {request} · Факт {actual} RPM",
            {{"source",out.curve->temperatureSource},{"temperature",value(out.temperature)},{"request",value(out.requested)},{"actual",value(out.actual)}});
        out.source=formatMessage("База EC · {source} · ступень {stage}/9",{{"source",out.curve->temperatureSource},{"stage",std::to_string(out.curve->stage)}});
        if(out.curve->stage!=out.curve->demandStage)out.source+=" · "+translate("задержка EC");
    }else{
    out.legend=formatMessage(out.curve->requestDerived?
        "EC CPU {temperature} °C · Запрос≈{request}% · Факт {actual}%":
        "EC CPU {temperature} °C · Запрос {request}% · Факт {actual}%",
        {{"temperature",value(out.temperature)},{"request",value(out.requested)},{"actual",value(out.actual)}});
    out.source=formatMessage("База EC · пуск {start} °C · стоп <{stop} °C",
        {{"start",std::to_string(out.curve->startCelsius)},{"stop",std::to_string(out.curve->offBelowCelsius)}});
    if(out.curve->requestDerived)out.source+=" · "+translate("≈ расчёт");
    }
    if(out.curve->retained||!nativeDefaultFanFresh(*out.curve,2500))out.legend+=" · "+translate("последнее чтение");
    return out;
}
}
