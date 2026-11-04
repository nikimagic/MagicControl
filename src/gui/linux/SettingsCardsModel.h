#pragma once
#include "../common/BatteryDetails.h"
namespace mc::gtkui {
inline auto linuxSettingsCards(const DeviceProfile& profile,const BatteryReading& os,
        const EcBatteryReading& ec,const BatteryPowerPolicy& policy,bool logging){
    return gui::ui::settingsCardsModel(profile,os,ec,policy,logging);
}
}
