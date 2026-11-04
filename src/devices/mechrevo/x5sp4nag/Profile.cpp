#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcRegister.h"

namespace mc::devices {
namespace {
constexpr DeviceIdentityProbe probes[]{
    {ec::kBoardEcVersion, "ecOEM_EC_VER", ec::kBoardEcVersionExpected},
    {ec::kBoardSystemId, "SystemID", ec::kBoardSystemIdExpected},
    {ec::kBoardProjectId, "ProjectID", ec::kBoardProjectIdExpected},
};
}
// Exact board only. Linux has no matching EC backend, so these verified Windows
// capabilities do not enable Linux hardware commands. Firmware contracts apply.
const DeviceProfile& x5sp4nagProfile() noexcept {
    static constexpr std::string_view features[]{"fans","power","battery","lighting","smu telemetry"};
    static const DeviceProfile profile=[] {DeviceProfile p{
        "wu-jie-15-xpro", "MECHREVO", "WUJIE Series-X5SP4NAG", "WUJIE X5SP4NAG",
        "uniwill-x5sp4nag", "windows-acpi-driver", "windows-pawnio-amd-smu",
        {features}, probes, {}, "Wujie 15X Pro",4,DeviceClass::Laptop};
        p.customPreset={{255,255,255},5,{true,true},false,false,false,{}};return p;}();
    return profile;
}
}
