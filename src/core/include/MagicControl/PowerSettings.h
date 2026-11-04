#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <algorithm>
#include <stdexcept>
namespace mc {
// Optional settings extend the SAME Custom preset, never a per-device preset
// engine. Empty options preserve every older device's file and commands.
using PowerOptions=std::map<std::string,int>;
// Layout metadata only. These names/units never grant control or invent ranges.
struct PowerOptionDescriptor {std::string_view key,label,unit;};
struct PowerSetting {
    std::string key,label,unit;
    std::optional<int> actual,defaultValue;
    int minimum{},maximum{},step{};
    std::vector<int> discrete;
    bool writable{};
    [[nodiscard]] bool accepts(int value)const noexcept {
        if(!writable)return false;
        if(!discrete.empty())return std::find(discrete.begin(),discrete.end(),value)!=discrete.end();
        return step>0&&minimum<=value&&value<=maximum&&(value-minimum)%step==0;
    }
    friend bool operator==(const PowerSetting&,const PowerSetting&)=default;
};
}
