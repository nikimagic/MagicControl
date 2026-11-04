#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_gx6hg0u_f() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-gx6hg0u-f", "MECHREVO", "GX6HG0U-F", "Huan 16 Air",
        "", "", "", {}, {}};
    return profile;
}
}
