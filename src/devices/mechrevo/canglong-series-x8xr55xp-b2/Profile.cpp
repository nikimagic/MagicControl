#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_canglong_series_x8xr55xp_b2() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-canglong-series-x8xr55xp-b2", "MECHREVO", "CANGLONG Series-X8xR55xP-B2", "CANGLONG Series",
        "", "", "", {}, {}};
    return profile;
}
}
