#pragma once
#include "../common/BatteryDetails.h"
namespace mc::gui {
inline auto windowsSettingsCards(const DeviceProfile& profile,const BatteryReading& os,
        const EcBatteryReading& ec,const BatteryPowerPolicy& policy,bool logging,std::optional<int> targetVoltageMv={}){
    auto model=ui::settingsCardsModel(profile,os,ec,policy,logging,targetVoltageMv);
    model.sources[1]="Windows";
    return model;
}
}
