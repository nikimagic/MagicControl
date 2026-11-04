#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_canglong_series_x6xr55xy() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-canglong-series-x6xr55xy", "MECHREVO", "CANGLONG Series-X6xR55xY", "CANGLONG Series",
        "", "", "", {}, {}};
    return profile;
}
}
