#pragma once
#include "DeviceProfile.h"
#include <array>
#include <string_view>
namespace mc {
// Describes existing admitted CPU backends, never grants a device capability.
// Stock profiles do not contain cross-model fixed watt triples.
enum class StockCpuSource { Unavailable,WujieFirmwarePolicy,RamFirmwareLimits,HuaweiNativeMode,HpbsdFirmwareLimits,LenovoWmi };
inline constexpr StockCpuSource stockCpuSource(std::string_view id){
    if(id=="wu-jie-15-xpro")return StockCpuSource::WujieFirmwarePolicy;
    if(ramFanControlProfile(id))return StockCpuSource::RamFirmwareLimits;
    if(id=="huawei-hlyl-wxx9")return StockCpuSource::HuaweiNativeMode;
    if(id=="minisforum-hpbsd")return StockCpuSource::HpbsdFirmwareLimits;
    if(id=="lenovo-legion-kwcn")return StockCpuSource::LenovoWmi;
    return StockCpuSource::Unavailable;
}
inline constexpr int stockCpuProfileCount(std::string_view id){return id=="huawei-hlyl-wxx9"||id=="minisforum-hpbsd"?2:3;}
inline constexpr std::array<const char*,3> stockCpuProfileNames(std::string_view id){
    return id=="lenovo-legion-kwcn"?std::array<const char*,3>{"Quiet","Balanced","Performance"}:id=="minisforum-hpbsd"?std::array<const char*,3>{"Balance","Performance",""}:id=="huawei-hlyl-wxx9"?std::array<const char*,3>{"Balanced","Performance",""}:
        std::array<const char*,3>{"Office","Gaming","Turbo"};
}
}
