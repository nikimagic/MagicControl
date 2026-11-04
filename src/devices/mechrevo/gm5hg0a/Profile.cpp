#include "MagicControl/DeviceProfile.h"
#include <array>
namespace mc::devices {
const DeviceProfile& reported_gm5hg0a() noexcept {
    static constexpr std::string_view features[]{"fans","power","battery","lighting"};
    // Owner authorizes common GM6 RamFan1.5 commands. Runtime requires this
    // exact SMBIOS, reviewed driver, stable non-fill ProjectID and RAM1.5 bit.
    static constexpr std::array probes{DeviceIdentityProbe{0x78E,"RamFan1.5 capability",0x40,0x40}};
    static constexpr RamFanProfileData ram{true,true,true,RamFanProjectRule::NonFill,100,200,
        "GM5HG0A","gm5hg0a-device-settings.ini","gm5hg0a-power.txt","fan-curves.csv"};
    static const DeviceProfile profile=[] {DeviceProfile p{
        "mechrevo-gm5hg0a","MECHREVO","GM5HG0A","Yilong15Pro GM5HG0A",
        "uniwill-gm5hg0a","windows-acpi-driver","",{features},probes,
        "Yilong15Pro Series GM5HG0A",{},4,DeviceClass::Laptop,&ram};
        p.customPreset={{ram.amdSustainedMaximum,ram.amdSustainedMaximum,ram.amdPeakMaximum},5,{true,true},false,false,true,{}};return p;}();
    return profile;
}
}
