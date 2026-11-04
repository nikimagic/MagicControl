#include "MagicControl/DeviceProfile.h"
#include "../../../protocols/minisforum/HpbsdPower.h"
namespace mc::devices {
const DeviceProfile& hpbsdProfile() noexcept {
    static constexpr std::string_view features[]{"fans","smu telemetry","power"};
    static constexpr std::string_view absent[]{"battery","keyboard-lighting"};
    // Owner HPBSD/AMI1.06/EC0.2/Ryzen8845HS evidence,26.10.2026.
    // Native CPU + ITE5571 telemetry has an independent exact runtime guard.
    // No Uniwill RAM map, charge/RGB protocol or fan writer is inherited.
    static const DeviceProfile profile=[] {DeviceProfile p{
        "minisforum-hpbsd","Micro Computer (HK) Tech Limited","HPBSD","EliteMini · HPBSD",
        "minisforum-hpbsd","","windows-pawnio-amd-telemetry",{features}, {},"EliteMini Series",{},0,DeviceClass::MiniPc,nullptr,{absent},{8,3}};
        p.customPreset={{hpbsd::customPowerMaximum,hpbsd::customPowerMaximum,hpbsd::customPowerMaximum},5,{true,true},true,true,false,{}};return p;}();
    return profile;
}
}
