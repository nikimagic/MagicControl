#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& reported_kuangshi_jiguang_series_x6xx55xw_b2() noexcept {
    // Inventory evidence only. No EC protocol, probes or write capabilities.
    static const DeviceProfile profile{
        "mechrevo-kuangshi-jiguang-series-x6xx55xw-b2", "MECHREVO", "KUANGSHI-JIGUANG Series-X6xx55xW-B2", "KUANGSHI Series",
        "", "", "", {}, {}};
    return profile;
}
}
