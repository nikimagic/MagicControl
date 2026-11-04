#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_yaoshi_series_x6ar55xy() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-yaoshi-series-x6ar55xy", "MECHREVO", "YAOSHI Series-X6AR55xY", "YAOSHI Series",
        "", "", "", {}, {}};
    return profile;
}
}
