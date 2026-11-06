#pragma once
#include "MagicControl/NativeDefaultFan.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace mc { class EcAccess; }
namespace mc::gm6 {
// The collector supplies one coherent context; no member is a write request.
struct DefaultFanContext {
    // version, project, SKU, SKU selector, state, 466, 4FE, 6C2,
    // mode, 759, 75A, 768, APP, commit, 7A4, 7C7, 7C8, 49F.
    std::array<std::uint8_t,18> context{};
    // I2EC: 08E6,08E7 (table-group pointer),08B8,08EB.
    std::array<std::uint8_t,4> selectors{};
    std::array<std::uint8_t,2> temperature{},request{},pwm{};
};
NativeDefaultFanReading decodeFirmwareDefaultFans(std::string_view actualProfile,const DefaultFanContext&,std::uint64_t sampledAtMs);
// Shared core/protocol reader. It is called only by the existing GM6 owner.
NativeDefaultFanReading readFirmwareDefaultFans(EcAccess&);
}
