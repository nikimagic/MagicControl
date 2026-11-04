#include "MagicControl/DeviceProfile.h"
#include <array>
namespace mc::devices {
const DeviceProfile& gm6ir0cProfile() noexcept {
    static constexpr std::string_view features[]{"fans","power","battery","lighting"};
    // Exact SMBIOS + OEM ProjectID. Controller additionally requires RamFan1.5
    // capability and checks these again before every explicit write.
    static constexpr std::array probes{DeviceIdentityProbe{0x740,"GM6IR0C ProjectID",25}};
    static constexpr RamFanProfileData ram{true,false,false,RamFanProjectRule::Exact25,100,200,
        "GM6IR0C","gm6ir0c-device-settings.ini","gm6ir0c-power.txt","fan-curves.csv"};
    static const DeviceProfile profile=[] {DeviceProfile p{"mechrevo-gm6ir0c","MECHREVO","GM6IR0C",
        "JiguangX GM6IR0C","uniwill-gm6ir0c","windows-acpi-driver","",
        {features},probes,"JiguangX Series GM6IR0C",{},4,DeviceClass::Laptop,&ram};
        p.customPreset={{140,165,250},10,{true,true},false,false,false,"gm6ir0c-custom-presets.txt"};return p;}();
    return profile;
}
}
