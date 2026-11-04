#include "MagicControl/IEcBackend.h"

namespace mc {

std::vector<std::uint8_t> IEcBackend::readBlock(std::uint16_t address, std::size_t count) {
    std::vector<std::uint8_t> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(read(static_cast<std::uint16_t>(address + i)));
    }
    return out;
}

}  // namespace mc
