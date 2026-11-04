#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& unknownProfile() noexcept {
    static const DeviceProfile profile{"", "", "", "", "", "", "", {}, {}};
    return profile;
}
}
