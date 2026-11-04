#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_gm6px7x() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-gm6px7x", "MECHREVO", "GM6PX7X", "Kuangshi16 Super Series GM6PX7X",
        "", "", "", {}, {}};
    return profile;
}
}
