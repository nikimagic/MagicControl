#include "MagicControl/DeviceProfile.h"
namespace mc::devices {
const DeviceProfile& l16Profile() noexcept {
    static constexpr std::string_view features[]{"fans","power","battery","lighting"};
    // Exact owner SMBIOS. The original N.1.13MAC10 DSDT documents ECRR;
    // ControlCenterX5.60.60.17 ID5/ID6DR_FR ships the reviewed10.48 driver.
    // Owner dump0.1.149: EC Project1A; BIOS74C low6=29/ID6,7CA.0=1.
    // RAM capability78E.6, native tables,
    // battery XIF/XST and RGB. Typed controller, never legacy X5 startup.
    static constexpr DeviceIdentityProbe probes[]{
        {0x740,"L16 EC ProjectID",0x1A},{0x78E,"RAM fan capability",0x40,0x40},
        {0x74C,"L16 BIOS project ID6",29,0x3F},{0x7CA,"Six-bit BIOS project",1,1}};
    static constexpr RamFanProfileData ram{true,true,true,RamFanProjectRule::Exact26,145,174,
        "L16","l16-device-settings.ini","l16-power.txt","l16-fan-curves.csv"};
    static const DeviceProfile profile=[] {DeviceProfile p{
        "machenike-l16","MACHENIKE","L16","L16 S · Ryzen 9 7945HX",
        "uniwill-l16","windows-acpi-driver","",{features},probes,
        "L16","L16 S · ID6 DR/FR",4,DeviceClass::Laptop,&ram};
        p.customPreset={{ram.amdSustainedMaximum,ram.amdSustainedMaximum,ram.amdPeakMaximum},5,{true,true},false,false,true,{}};return p;}();
    return profile;
}
}
