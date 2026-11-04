#include "MagicControl/DeviceProfile.h"
#include "../../../protocols/uniwill/hydroc16g2/Diagnostics.h"
#include <array>
namespace mc::devices {
const DeviceProfile& hydroc16g2Profile() noexcept {
    static constexpr std::string_view features[]{"fans","power","battery","lighting","liquid cooling"};
    // Explicit integration: own OEM RAM1.5 + typed HID discovery. Runtime
    // BIOS/driver/P13/LC guards apply independently to each operation.
    static constexpr std::array probes{DeviceIdentityProbe{0x740,"HYDROC ProjectID",26},
        DeviceIdentityProbe{0x78E,"HYDROC RAM1.5",0x40,0x40},DeviceIdentityProbe{0x7C3,"HYDROC platform13",13}};
    static constexpr RamFanProfileData ram{false,false,true,RamFanProjectRule::Exact26,100,200,
        "HYDROC-16 G2","hydroc16g2-device-settings.ini","hydroc16g2-power.txt","hydroc16g2-fan-curves.csv"};
    static const DeviceProfile profile=[] {DeviceProfile p{hydroc16g2::profileId,"Eluktronics Inc.","HYDROC-16",
        "HYDROC-16 G2",hydroc16g2::protocolId,"windows-hydroc-ramfan","",
        {features},probes,"HYDROC-16 G2",{},4,DeviceClass::Laptop,&ram};
        p.customPreset={{255,255,510},5,{true,true},false,false,false,{}};return p;}();
    return profile;
}
}
