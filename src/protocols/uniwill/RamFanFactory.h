#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>

namespace mc::uniwill {
// The same mailbox is independently present in the exact GM6 and HYDROC OEM
// FanTable_Manager1p5.RefreshDefaultFanTable bodies. This is an ACTIVE request,
// not flash imaging and never a passive polling/startup operation.
struct RamFanFactoryIo {
    std::function<std::uint8_t(std::uint16_t)> read;
    std::function<void(std::uint16_t,std::uint8_t)> writeCommand;
    // Recheck exact board/SYS where applicable, OEM ownership and admission.
    // Caller also holds its controller gate and the cross-process EC transaction.
    std::function<void()> guard;
};
struct RamFanFactoryRaw {
    std::array<std::uint8_t,96> bytes{},valid{};
};
struct RamFanFactorySample {
    int mode{};std::string name,status;
    bool ready{},valid{};unsigned waits{};
    RamFanFactoryRaw raw;
};
struct RamFanFactoryResult {
    bool attempted{},complete{},restored{},cancelled{};
    bool defaultBefore{},defaultAfter{};
    int expectedProject{-1};
    std::optional<std::uint8_t> appBefore,appAfter;
    std::string status,json;
    RamFanFactoryRaw rawBefore,rawAfter;
    std::array<RamFanFactorySample,3> samples;
};
// Explicit WithFanProbes is mandatory; false performs no I/O. Default must be
// established by a typed controller before entry. Never activates/replays RAM.
RamFanFactoryResult exportRamFanFactory(const RamFanFactoryIo&,std::uint8_t expectedProject,
                                       bool explicitWithFanProbes,std::stop_token={});
RamFanFactoryResult refusedRamFanFactory(std::string status,bool cancelled=false);
}
