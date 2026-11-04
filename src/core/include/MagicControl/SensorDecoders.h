#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <cstring>
#include <array>
namespace mc {
inline std::optional<double> batteryTemperatureCelsius(std::uint32_t tenthsKelvin) {
    const double value=tenthsKelvin/10.0-273.15;
    return tenthsKelvin && tenthsKelvin!=UINT32_MAX && value>=-40 && value<=100
        ? std::optional<double>(value):std::nullopt;
}
inline std::optional<double> spd5118TemperatureCelsius(std::uint16_t word) {
    int value=(word>>2)&0x7ff;
    if(value&0x400)value-=0x800;
    const double celsius=value/4.0;
    return celsius>=-20 && celsius<=125 ? std::optional<double>(celsius):std::nullopt;
}
// Ordinary DDR5 only. UMC DBG_MISC low3 is the CURRENT memory P-state;
// CFG_MISC[pstate].low16 is its nominal clock in MHz. Never use P0 blindly
// or decode LPDDR using DDR5's factor two. Sources: CoreFreq amd_reg.h and
// ZenStates-Core Ddr5Timings.ReadRatio; live Strix Point observation 02.11.2026.
inline std::optional<unsigned> zenDdr5ActiveClock(std::uint32_t before,std::uint32_t config,std::uint32_t after){
    if(before==UINT32_MAX||after==UINT32_MAX||config==UINT32_MAX||
       (before&0x30)!=0x30||(after&0x30)!=0x30||((before^after)&7)||
       (before&7)>3)return {};
    const unsigned mhz=config&0xffff;
    return mhz>=500&&mhz<=10000?std::optional<unsigned>(mhz):std::nullopt;
}
// Ordinary DDR4: only the reviewed P0 page is admitted.
inline std::optional<double> zenDdr4ActiveClock(std::uint32_t before,std::uint32_t config,std::uint32_t after){
    if(before==UINT32_MAX||after==UINT32_MAX||config==UINT32_MAX||
       (before&0x30)!=0x30||(after&0x30)!=0x30||((before^after)&7)||(before&7))return {};
    const auto ratio=config&0x7f;
    return ratio>=15&&ratio<=63?std::optional<double>(ratio*(100.0/3.0)):std::nullopt;
}
inline std::optional<double> zenMemoryChannelsClock(bool ddr5,const std::array<std::uint32_t,2>& before,
    const std::array<std::uint32_t,2>& config,const std::array<std::uint32_t,2>& after){
    const auto decode=[&](unsigned channel)->std::optional<double>{
        if(!ddr5)return zenDdr4ActiveClock(before[channel],config[channel],after[channel]);
        const auto value=zenDdr5ActiveClock(before[channel],config[channel],after[channel]);
        return value?std::optional<double>(*value):std::nullopt;
    };
    const auto first=decode(0),second=decode(1);
    return first&&second&&first==second?first:std::nullopt;
}
enum class IntelMemoryClockStatus { Transition, Unavailable, Valid };
struct IntelMemoryClock { IntelMemoryClockStatus status; double mhz{}; };
inline IntelMemoryClock intelMemoryClock(std::uint32_t before,std::uint32_t bios,std::uint32_t after){
    if((before&0x7fc)!=(after&0x7fc))return {IntelMemoryClockStatus::Transition};
    const auto ratio=(after>>2)&0xff,gear=(bios>>12)&3;
    if(ratio<3||gear>2)return {IntelMemoryClockStatus::Unavailable};
    const double clock=ratio*((after&0x400)?100.0:400.0/3.0)*(1u<<gear);
    if(clock<400||clock>5000)return {IntelMemoryClockStatus::Unavailable};
    return {IntelMemoryClockStatus::Valid,clock};
}
inline int signedBatteryWord(std::uint16_t raw){return raw>=0x8000?int(raw)-65536:int(raw);}
inline std::optional<int> lenovoBatteryCurrent(std::optional<std::uint16_t> raw){
    if(!raw)return {};
    const int value=signedBatteryWord(*raw);
    return value>=-30000&&value<=30000?std::optional<int>(value):std::nullopt;
}
template<class T> inline std::optional<T> sensorField(std::span<const std::uint8_t> bytes,std::size_t offset) {
    if(offset>bytes.size() || sizeof(T)>bytes.size()-offset)return {};
    T value{};std::memcpy(&value,bytes.data()+offset,sizeof(T));return value;
}
inline std::optional<double> storageTemperatureCelsius(std::span<const std::uint8_t> bytes) {
    const auto version=sensorField<std::uint32_t>(bytes,0),size=sensorField<std::uint32_t>(bytes,4);
    const auto count=sensorField<std::uint16_t>(bytes,12);
    if(!version||!size||!count||*version<40||*size<40||*size>bytes.size()||
        *count>(*size-24)/16)return {};
    std::optional<double> fallback;
    for(std::size_t i=0;i<*count;++i){
        const auto index=*sensorField<std::uint16_t>(bytes,24+16*i);
        const auto value=*sensorField<std::int16_t>(bytes,26+16*i);
        if(value<-40||value>150)continue; // Includes the unavailable sentinel 0x8000.
        if(index==0)return value;
        if(!fallback)fallback=value;
    }
    return fallback;
}
inline std::optional<double> nvmeSmartTemperatureCelsius(std::span<const std::uint8_t> bytes) {
    // STORAGE_PROTOCOL_DATA_DESCRIPTOR: 8-byte header, 40-byte protocol data.
    if(sensorField<std::uint32_t>(bytes,0)!=48 || sensorField<std::uint32_t>(bytes,4)!=48 ||
       sensorField<std::uint32_t>(bytes,8)!=3 || sensorField<std::uint32_t>(bytes,12)!=2)return {};
    const auto offset=sensorField<std::uint32_t>(bytes,24),length=sensorField<std::uint32_t>(bytes,28);
    if(!offset||!length||*offset<40||*length<512||bytes.size()<8||
       *offset>bytes.size()-8||*length>bytes.size()-8-*offset)return {};
    const auto kelvin=sensorField<std::uint16_t>(bytes,8+*offset+1);
    if(!kelvin||!*kelvin||*kelvin==65535)return {};
    const double value=*kelvin-273.15;
    return value>=-40&&value<=150?std::optional<double>(value):std::nullopt;
}
}
