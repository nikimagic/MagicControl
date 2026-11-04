#include "MagicControl/DeviceProfile.h"
#include <array>
namespace mc::devices {
const DeviceProfile& reported_gm6px7x() noexcept;
const DeviceProfile& reported_kuangshi_jiguang_series_x6xx55xw_b2() noexcept;
const DeviceProfile& reported_canglong_series_x6xr55xy() noexcept;
const DeviceProfile& reported_gm6bg0x() noexcept;
const DeviceProfile& reported_kuangshi_jiguang_series_x6xx57tw() noexcept;
const DeviceProfile& reported_jiaolong_series_x6dr55xx_b2() noexcept;
const DeviceProfile& reported_gx6hg0u_f() noexcept;
const DeviceProfile& reported_canglong_series_x8xr55xp_b2() noexcept;
const DeviceProfile& reported_yaoshi_series_x6ar55xy() noexcept;
const DeviceProfile& reported_gm6ix9b() noexcept;
const DeviceProfile& reported_gm5hg0a() noexcept;
std::span<const DeviceProfile* const> reportedMechrevoProfiles() noexcept {
    static const std::array profiles{&reported_gm6px7x(),
        &reported_kuangshi_jiguang_series_x6xx55xw_b2(),
        &reported_canglong_series_x6xr55xy(),
        &reported_gm6bg0x(),
        &reported_kuangshi_jiguang_series_x6xx57tw(),
        &reported_jiaolong_series_x6dr55xx_b2(),
        &reported_gx6hg0u_f(),
        &reported_canglong_series_x8xr55xp_b2(),
        &reported_yaoshi_series_x6ar55xy(),
        &reported_gm6ix9b(),
        &reported_gm5hg0a()};
    return profiles;
}
}
