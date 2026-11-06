#pragma once
#include "SensorDecoders.h"
#include <cstdint>
#include <optional>

namespace mc {
// Observation selection depends only on the CPU/register layout and the memory
// technology, never on an SMBIOS model or selected UI preview.
enum class MemoryCpuVendor { Other, Intel, Amd };
enum class MemoryClockBackend { Unavailable, IntelQclk, AmdDdr4, AmdDdr5 };
[[nodiscard]] inline constexpr MemoryClockBackend memoryClockBackend(
        MemoryCpuVendor vendor,unsigned family,unsigned model,bool ddr4,bool ddr5) noexcept {
    if(ddr4==ddr5)return MemoryClockBackend::Unavailable;
    if(vendor==MemoryCpuVendor::Intel&&family==6&&
       (model==0x97||model==0x9a||model==0xb7||model==0xba||model==0xbf))
        return MemoryClockBackend::IntelQclk;
    if(vendor==MemoryCpuVendor::Amd){
        const bool modern=(family==0x1a&&(model==0x20||model==0x24))||
                          (family==0x19&&(model==0x74||model==0x75));
        const bool legacy=(family==0x17&&(model==0x60||model==0x71))||
                          (family==0x19&&(model==0x21||model==0x50));
        if(ddr5&&modern)return MemoryClockBackend::AmdDdr5;
        if(ddr4&&legacy)return MemoryClockBackend::AmdDdr4;
    }
    return MemoryClockBackend::Unavailable;
}

struct IntelMemoryCapture {
    IntelMemoryClockStatus status{IntelMemoryClockStatus::Unavailable};
    std::optional<std::uint32_t> before,bios,after;
    std::optional<double> mhz;
    unsigned attempts{};
};
[[nodiscard]] inline constexpr bool memoryRetryDue(std::uint64_t now,std::uint64_t retryAt,bool diagnostic) noexcept {
    return diagnostic||now>=retryAt;
}
// One bounded request from the existing monitoring owner; callers provide the
// admitted I/O. This helper cannot discover/open a backend or write hardware.
template<class ReadStatus,class ReadBios>
[[nodiscard]] IntelMemoryCapture captureIntelMemoryClock(bool admitted,ReadStatus&& readStatus,ReadBios&& readBios){
    IntelMemoryCapture out;
    if(!admitted)return out;
    for(unsigned attempt=0;attempt<3;++attempt){
        ++out.attempts;
        out.before=readStatus();out.bios=readBios();out.after=readStatus();
        const auto decoded=intelMemoryClock(*out.before,*out.bios,*out.after);
        out.status=decoded.status;
        if(decoded.status==IntelMemoryClockStatus::Transition)continue;
        if(decoded.status==IntelMemoryClockStatus::Valid)out.mhz=decoded.mhz;
        break;
    }
    return out;
}
}
