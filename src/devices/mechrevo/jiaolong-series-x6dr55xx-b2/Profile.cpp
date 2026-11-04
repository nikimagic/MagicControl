#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_jiaolong_series_x6dr55xx_b2() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-jiaolong-series-x6dr55xx-b2", "MECHREVO", "JIAOLONG Series-X6DR55xx-B2", "JIAOLONG Series",
        "", "", "", {}, {}};
    return profile;
}
}
