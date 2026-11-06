#pragma once
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace mc {
// Packed SBS manufacture date. Decoding is shared; it does not authorize a
// device read or claim that an EC cache is a direct BMS observation.
inline std::string decodeSbsBatteryDate(std::uint8_t high,std::uint8_t low){
    const auto raw=(unsigned(high)<<8)|low;
    if(raw==0||raw==0xffff)return {};
    const std::chrono::year_month_day date{std::chrono::year{1980+int(raw>>9)},
        std::chrono::month{(raw>>5)&15},std::chrono::day{raw&31}};
    if(!date.ok())return {};
    return std::format("{:04}-{:02}-{:02}",int(date.year()),unsigned(date.month()),unsigned(date.day()));
}
// The device adapter supplies the byte reader and admits the physical address.
// A stable invalid date is an empty string; an unstable pair is unknown.
template<class ReadByte>
std::optional<std::string> readStableSbsBatteryDate(ReadByte&& read){
    for(int attempt=0;attempt<3;++attempt){
        const auto high=read(0),low=read(1),againHigh=read(0),againLow=read(1);
        if(high==againHigh&&low==againLow)return decodeSbsBatteryDate(high,low);
    }
    return {};
}
}
