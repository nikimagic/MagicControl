#pragma once
#include <array>
#include <optional>
#include <string>
#include "../MetricLine.h"
#include "../DisplayFormat.h"
namespace mc::gui::ui {
inline std::string monitoringNumber(double value,int precision){return displayNumber(value,precision);}
inline std::string monitoringMetric(std::optional<double> value,int precision=0,bool trimZero=false){
    if(!value)return "—";auto text=monitoringNumber(*value,precision);
    if(trimZero&&(text.ends_with(",0")||text.ends_with(".0")))text.resize(text.size()-2);return text;
}
inline std::string monitoringGrouped(std::optional<double> value){
    if(!value)return "—";auto text=monitoringNumber(*value,0);
    for(int i=static_cast<int>(text.size())-3;i>0;i-=3)text.insert(static_cast<std::size_t>(i)," ");return text;
}
// One existing Wujie card model/renderer, reused by CPU and vendor GPU files.
struct ProcessorCard {
    std::string title;
    std::array<std::string,3> values{};
    std::array<std::string,3> valueUnits{"","%","°C"};
    std::array<std::string,3> sources{};
    std::array<InlineMetric,3> summary{};
    std::array<std::string,3> information{};
    std::string secondaryTitle,secondaryValue,secondaryUnit,secondarySource;
    std::string secondaryExtraTitle,secondaryExtraValue,secondaryExtraUnit;
    std::string selectionReason; // Diagnostic projection; never a control input.
};
}
