#pragma once
#include "MagicControl/Monitoring.h"
#include "MagicControl/SensorDecoders.h"
#include "MagicControl/BatteryDate.h"
#include <algorithm>
#include <span>
#include <iterator>
#include <chrono>
#include <format>

namespace mc::uniwill::x5sp4nag {
// X5SP4NAG firmware copies this packed SBS-format date to EC RAM 0x314/315
// in big-endian order. This is an EC cache, not a direct BMS or BIOS read.
inline std::string decodeBatteryManufactureDate(std::uint8_t high,std::uint8_t low){
    return decodeSbsBatteryDate(high,low);
}
// Pure decoding only. The caller admits an exact-board coherent sample before
// calling; this function cannot open a backend, read hardware or write it.
inline constexpr std::uint16_t batteryCoreAddresses[]{0x400,0x401,0x402,0x403,0x404,0x405,
            0x432,0x433,0x434,0x435,0x436,0x437,0x438,0x439,
            0x4A2,0x4A3,0x4A6,0x4A7,0x4AB,
            0x7B9,0x7D0,0x7C3,0x7A6,0x7CC,0x7CD,0x7CE,0x490,0x497,0x742,
            0x520,0x521,0x522,0x523,0x30E,0x30F};
inline EcBatteryReading decodeBatteryCore(std::span<const std::uint8_t> raw){
    EcBatteryReading result;result.supported=true;result.profileId="wu-jie-15-xpro";
    if(raw.size()!=std::size(batteryCoreAddresses)){result.status="Incomplete battery core sample";return result;}
    const auto byte=[&](std::uint16_t address){return raw[static_cast<std::size_t>(std::find(std::begin(batteryCoreAddresses),std::end(batteryCoreAddresses),address)-std::begin(batteryCoreAddresses))];};
    const auto word=[&](std::uint16_t address){return static_cast<std::uint16_t>(byte(address)|(static_cast<unsigned>(byte(address+1))<<8));};
    result.coherent=true;result.status="Согласованный снимок EC";
    result.state=word(0x432);result.readyRaw=byte(0x490);result.isdbRaw=byte(0x497);result.gateRaw=byte(0x742);
    result.upperRaw=byte(0x7B9);result.restartRaw=byte(0x7D0);result.platformRaw=byte(0x7C3);
    result.voltagePolicyRaw=byte(0x7A6);result.chargerRaw=byte(0x7CC);
    result.typeCRatingA=byte(0x7CD);result.typeCRatingB=byte(0x7CE);
        // These words have no exact firmware consumer yet. Keep raw labels;
        // neither a requested voltage nor a current limit is asserted here.
    result.raw520=word(0x520);result.raw522=word(0x522);
    result.rawBase=static_cast<std::uint16_t>((static_cast<unsigned>(byte(0x30E))<<8)|byte(0x30F));
    auto& battery=result.battery;battery.source="EC · X5SP4NAG";battery.deviceId="EC/X5SP4NAG/BAT0";
    if(*result.readyRaw!=255)battery.present=bool(*result.readyRaw&2);
    if(*result.readyRaw==255||!(*result.readyRaw&4)||battery.present!=true){result.status="Батарея не готова по EC";battery.healthUnavailable=true;return result;}
    // The exact X5SP4NAG DSDT fixes _BIF Power Unit to 1 (mAh/mA) and maps
    // XIF1/XIF2/XST1/XST2 to these EC words. XIF0 at 0x400 is not used as
    // _BIF's Power Unit; it can be zero while these fields remain available.
    const auto capacity=[&](std::uint16_t a)->std::optional<std::uint16_t>{const auto v=word(a);return v&&v!=0xFFFF?std::optional(v):std::nullopt;};
    result.designMah=capacity(0x402);result.fullMah=capacity(0x404);
    const auto remaining=word(0x436);if(remaining!=0xFFFF)result.remainingMah=remaining;
    if(byte(0x4AB)<=100)battery.percent=byte(0x4AB);
    const auto current=word(0x434),voltage=word(0x438),temperature=word(0x4A2),cycles=word(0x4A6);
    if(current<=30000)battery.milliamps=current;
    if(voltage>=5000&&voltage<=25000)battery.volts=voltage/1000.;
    battery.celsius=batteryTemperatureCelsius(temperature);
    battery.temperatureRaw=temperature;battery.temperatureSource=battery.source;
    if(cycles!=0xFFFF)battery.cycles=cycles;
    if(result.designMah&&result.fullMah)battery.healthPercent=100.* *result.fullMah / *result.designMah;
    battery.healthUnavailable=!battery.healthPercent.has_value();
    const auto state=*result.state&3;
    battery.flow=state==1?BatteryFlow::Discharging:state==2?BatteryFlow::Charging:state==0?BatteryFlow::Idle:BatteryFlow::Unknown;
    if(battery.volts&&battery.milliamps&&state!=3&&(state!=0||*battery.milliamps==0))
        battery.watts=state==0?0.:*battery.volts * *battery.milliamps/1000.*(state==1?-1.:1.);
    return result;
}
}
