#include "MagicControl/DeviceProfile.h"
#include "../../../protocols/lenovo/LegionWmi.h"
namespace mc::devices {
const DeviceProfile& legionKwcnProfile()noexcept{
    static constexpr std::string_view features[]{"fans","power","battery","lighting"};
    static constexpr std::string_view variants[]{"82WQ"};
    static constexpr auto options=[] {std::array<PowerOptionDescriptor,lenovo::optionIds.size()> result{};
        for(std::size_t i=0;i<result.size();++i){const auto& p=lenovo::optionIds[i];result[i]={p.key,p.label,p.unit};}return result;}();
    // Owner logs:82WK/LNVNB161216/KWCN54WW; owner board label NM-F081.
    // 82WQ is the exact sibling in the OEM KWCN family. WMI GUIDs, actual
    // capabilities, SKU tables and native readback still authorize each write.
    static const DeviceProfile profile=[] {DeviceProfile p{"lenovo-legion-kwcn","LENOVO","LNVNB161216","Legion · KWCN",
        "lenovo-legion-kwcn-wmi","","",{features},{},"82WK",{},2,DeviceClass::Laptop,nullptr,{}, {10,10},{variants},{options}};
        // Cross Loading is independent of PL1. Retain existing file admission;
        // all actual limits and writability are checked against fresh WMI.
        p.customPreset={{100000,100000,100000},5,{false,false},true,false,false,{}};
        p.batteryModeLabels=BatteryModeLabels::ChargeModes;return p;}();
    return profile;
}
}
