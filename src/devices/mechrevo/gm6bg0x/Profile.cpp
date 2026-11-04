#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_gm6bg0x() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-gm6bg0x", "MECHREVO", "GM6BG0X", "Jiaolong16K Series GM6BG0X",
        "", "", "", {}, {}};
    return profile;
}
}
