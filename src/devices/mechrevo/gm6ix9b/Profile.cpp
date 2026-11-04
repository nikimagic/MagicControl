#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_gm6ix9b() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-gm6ix9b", "MECHREVO", "GM6IX9B", "Yaoshi16Super Series GM6IX9B",
        "", "", "", {}, {}};
    return profile;
}
}
