#pragma once
#include <charconv>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
namespace mc::windows {
struct NpuPowerResult {std::optional<double> watts;std::string error;};
// Parse the existing read-only `examine --report platform --batch` output.
// N/A means no measurement, never zero. Preserve a supported numeric zero.
inline NpuPowerResult parseNpuPowerReport(const std::string& report){
    const auto trim=[](std::string_view v){const auto first=v.find_first_not_of(" \t\r");
        if(first==v.npos)return std::string_view{};return v.substr(first,v.find_last_not_of(" \t\r")-first+1);};
    std::istringstream lines(report);std::string line;
    while(std::getline(lines,line)){
        const auto colon=line.find(':');if(colon==line.npos)continue;
        const auto label=trim(std::string_view(line).substr(0,colon));
        if(label!="Power"&&label!="Estimated Power")continue;
        const auto text=trim(std::string_view(line).substr(colon+1));
        if(text.empty())continue;
        if(text=="N/A")return {{},"AMD XRT: Estimated Power=N/A; драйвер не предоставил мощность NPU"};
        double value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if(parsed.ec!=std::errc{}||!std::isfinite(value)||value<0||value>250)continue;
        const auto unit=trim(std::string_view(parsed.ptr,text.data()+text.size()-parsed.ptr));
        if(unit=="Watts"||unit=="W")return {value,{}};
    }
    return {{},"AMD XRT: в отчёте нет мощности NPU"};
}
}
