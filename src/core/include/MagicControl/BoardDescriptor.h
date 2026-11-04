#pragma once
#include <string>

namespace mc {
// SMBIOS description, not a unique machine identity or hardware admission.
struct BoardDescriptor {
    std::string manufacturer;
    std::string baseBoard;
    std::string modelId;
    std::string productName;

    [[nodiscard]] bool known() const noexcept { return !modelId.empty(); }
};
}
