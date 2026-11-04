#pragma once
#include "FirmwareEvidence.h"
#include <chrono>
#include <cmath>
#include <format>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mc::diagnostics {
struct DisplayIssue {
    std::string field,source,reason,errorDomain;
    std::optional<std::uint32_t> errorCode;
};
struct DisplayReading {
    std::string id,name,source,status,modeSource,refreshSource,scaleSource,dpiSource;
    std::optional<bool> active,primary,connected;
    std::optional<unsigned> pixelWidth,pixelHeight;
    std::optional<std::uint64_t> refreshNumerator,refreshDenominator;
    std::optional<double> refreshHz,scalePercent,dpiX,dpiY;
    std::vector<DisplayIssue> issues;
    // Only internally constructed JSON; raw names/paths must use firmwareQuote.
    std::string metadataJson{"{}"};
};
struct DisplayEvidence {
    std::string source,status,metadataJson{"{}"};
    bool enumerationComplete{},cancelled{};
    std::vector<DisplayReading> displays;
    std::vector<DisplayIssue> issues;
};
inline std::string displayNumber(std::optional<double> value){
    if(!value||!std::isfinite(*value))return "null";
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(15)<<*value;return out.str();
}
template<class T>inline std::string displayInteger(std::optional<T> value){return value?std::to_string(*value):"null";}
inline std::string displayBoolean(std::optional<bool> value){return value?(*value?"true":"false"):"null";}
inline std::string displayIssuesJson(const std::vector<DisplayIssue>& issues){
    std::string out="[";bool first=true;for(const auto& issue:issues){if(!first)out+=',';first=false;
        out+="{\"field\":"+firmwareQuote(issue.field)+",\"source\":"+firmwareQuote(issue.source)+",\"reason\":"+firmwareQuote(issue.reason)+
            ",\"errorDomain\":"+firmwareQuote(issue.errorDomain)+",\"errorCode\":"+displayInteger(issue.errorCode)+"}";}
    return out+"]";
}
inline std::string displayEvidenceJson(const DisplayEvidence& value){
    const auto utc=std::format("{:%Y-%m-%dT%H:%M:%SZ}",std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
    bool complete=value.enumerationComplete&&!value.cancelled;
    for(const auto& d:value.displays)complete=complete&&d.active.has_value()&&d.primary.has_value()&&d.pixelWidth&&d.pixelHeight&&d.refreshHz&&d.scalePercent;
    std::string out="{\"schema\":1,\"utc\":"+firmwareQuote(utc)+",\"readOnly\":true,\"snapshotAtomic\":false,\"source\":"+firmwareQuote(value.source)+
        ",\"enumerationComplete\":"+(value.enumerationComplete?"true":"false")+",\"currentDataComplete\":"+(complete?"true":"false")+
        ",\"cancelled\":"+(value.cancelled?"true":"false")+",\"status\":"+firmwareQuote(value.status)+",\"issues\":"+displayIssuesJson(value.issues)+",\"displays\":[";
    bool first=true;for(const auto& d:value.displays){if(!first)out+=',';first=false;
        out+="{\"id\":"+firmwareQuote(d.id)+",\"name\":"+firmwareQuote(d.name)+",\"source\":"+firmwareQuote(d.source)+",\"status\":"+firmwareQuote(d.status)+
            ",\"active\":"+displayBoolean(d.active)+",\"primary\":"+displayBoolean(d.primary)+",\"connected\":"+displayBoolean(d.connected)+
            ",\"pixelWidth\":"+displayInteger(d.pixelWidth)+",\"pixelHeight\":"+displayInteger(d.pixelHeight)+",\"modeSource\":"+firmwareQuote(d.modeSource)+
            ",\"refreshHz\":"+displayNumber(d.refreshHz)+",\"refreshRate\":{\"numerator\":"+displayInteger(d.refreshNumerator)+",\"denominator\":"+displayInteger(d.refreshDenominator)+"},\"refreshSource\":"+firmwareQuote(d.refreshSource)+
            ",\"scalePercent\":"+displayNumber(d.scalePercent)+",\"scaleSource\":"+firmwareQuote(d.scaleSource)+",\"effectiveDpi\":{\"x\":"+displayNumber(d.dpiX)+",\"y\":"+displayNumber(d.dpiY)+"},\"dpiSource\":"+firmwareQuote(d.dpiSource)+
            ",\"issues\":"+displayIssuesJson(d.issues)+",\"metadata\":"+d.metadataJson+"}";}
    return out+"],\"metadata\":"+value.metadataJson+"}\n";
}
}
