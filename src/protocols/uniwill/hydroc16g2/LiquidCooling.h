#pragma once
#include "MagicControl/LiquidCooling.h"
#include <algorithm>
#include <stdexcept>
#include <string_view>
namespace mc::hydroc16g2::lpp {
// Exact OEM Demo.BLE / LiquidCooling from ControlCenter5.23.51.56.
inline constexpr std::string_view service="6e400001-b5a3-f393-e0a9-e50e24dcca9e";
inline constexpr std::string_view tx="6e400002-b5a3-f393-e0a9-e50e24dcca9e";
inline constexpr std::string_view rx="6e400003-b5a3-f393-e0a9-e50e24dcca9e";
inline constexpr std::uint16_t presence=0x7c7,capability=0x7c8,pumpTarget=0x7e7,fanTarget=0x7e6;
inline constexpr std::uint8_t presenceMask=2,capabilityMask=0x20;
using Packet=std::array<std::uint8_t,8>;
constexpr bool knownDevice(std::string_view name){return name=="CoolingSystem LCT21001"||name=="CoolingSystem LCT22002";}
constexpr bool knownVoltage(PumpVoltage value){return value==PumpVoltage::V7||value==PumpVoltage::V8||value==PumpVoltage::V11||value==PumpVoltage::V12;}
inline Packet pump(const LiquidPumpCommand& c){
    if(!knownVoltage(c.voltage)||c.duty<0||c.duty>100||(c.enabled&&!c.duty))throw std::invalid_argument("LPP: invalid pump voltage/duty");
    const std::uint8_t voltage=c.voltage==PumpVoltage::V11?0:c.voltage==PumpVoltage::V12?1:c.voltage==PumpVoltage::V7?2:3;
    return {0xfe,0x1c,static_cast<std::uint8_t>(c.enabled),static_cast<std::uint8_t>(c.enabled?c.duty:0),static_cast<std::uint8_t>(c.enabled?voltage:0),0,0,0xef};
}
inline Packet fan(const LiquidFanCommand& c){
    if(c.duty<0||c.duty>100||(c.enabled&&!c.duty))throw std::invalid_argument("LPP: fan duty0..100; enabled fan needs nonzero duty");
    return {0xfe,0x1b,static_cast<std::uint8_t>(c.enabled),static_cast<std::uint8_t>(c.enabled?c.duty:0),0,0,0,0xef};
}
inline constexpr Packet meter{0xfe,0x32,0,0,0,0,0,0xef};
inline constexpr Packet lineOff{0xfe,0x38,0,0,0,0,0,0xef};
inline constexpr Packet sleep{0xfe,0x19,0,1,0,0,0,0xef};
inline LiquidCoolingTargets targets(std::uint8_t pumpRaw,std::uint8_t fanRaw){
    LiquidCoolingTargets out;out.pumpRaw=pumpRaw;out.fanRaw=fanRaw;
    const auto voltage=(pumpRaw&0x80)?pumpRaw&0x7f:0;
    if(voltage&&voltage!=7&&voltage!=8&&voltage!=11&&voltage!=12)throw std::runtime_error("LPP: unreviewed native pump-voltage code");
    out.pump={voltage!=0,60,voltage?static_cast<PumpVoltage>(voltage):PumpVoltage::V8};
    const int duty=(fanRaw&0x80)?fanRaw&0x7f:0;if(duty>100)throw std::runtime_error("LPP: native fan target exceeds100");
    out.fan={duty!=0,duty};return out;
}
inline bool allowedPacket(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=8||bytes[0]!=0xfe||bytes[7]!=0xef)return false;
    if(std::equal(bytes.begin(),bytes.end(),meter.begin())||std::equal(bytes.begin(),bytes.end(),lineOff.begin())||std::equal(bytes.begin(),bytes.end(),sleep.begin()))return true;
    if(bytes[2]>1||bytes[3]>100||bytes[5]||bytes[6])return false;
    if(bytes[1]==0x1b)return bytes[4]==0&&(bytes[2]?bytes[3]>0:bytes[3]==0);
    return bytes[1]==0x1c&&bytes[4]<=3&&(bytes[2]?bytes[3]>0:bytes[3]==0&&bytes[4]==0);
}
inline std::optional<int> meterState(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=8||bytes[0]!=0xfe||bytes[7]!=0xef||(bytes[1]!=0x31&&bytes[1]!=0x32))return {};
    return bytes[3];
}
inline std::optional<std::string> firmwareVersion(std::span<const std::uint8_t> bytes){
    if(bytes.size()<18||bytes.size()>128||bytes[0]==0xfe)return {};
    std::string text(bytes.begin(),bytes.end());while(!text.empty()&&(text.back()=='\0'||text.back()=='\r'||text.back()=='\n'))text.pop_back();
    if(text.size()<18)return {};for(unsigned char c:text)if(c<32||c>126)return {};
    // Exact Demo.BLE.MergeFWversion takes the suffix at17, then parses the
    // decimal version with dots removed. No guessed packet/RPM acknowledgement.
    auto version=text.substr(17);bool digit=false;for(char c:version){if(c>='0'&&c<='9')digit=true;else if(c!='.')return {};}
    return digit?std::optional<std::string>(version):std::nullopt;
}
}
