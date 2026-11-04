#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_kuangshi_jiguang_series_x6xx57tw() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-kuangshi-jiguang-series-x6xx57tw", "MECHREVO", "KUANGSHI-JIGUANG Series-X6xx57TW", "JIGUANG Series",
        "", "", "", {}, {}};
    return profile;
}
}
