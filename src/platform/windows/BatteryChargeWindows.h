#pragma once
#include <string>
#include "MagicControl/BatteryEcPolicy.h"
namespace mc { class EcAccess; }
namespace mc::windows {
// Explicit policy wins; legacy platform7 preserves an observed7/FF when editing
// dormant thresholds. setBatteryAutomatic explicitly selects7/04 or4/FF.
// Readback is not a long-term/reconnect physical enforcement claim.
// Exact-board percentage control; independent of fan ownership and fan tables.
std::string applyBatteryChargeLimit(EcAccess& ec, int percent, bool platform7 = false,int restart=-1,std::optional<BatteryEcPolicy> policy={},std::optional<unsigned> requiredPlatform={});
// Legacy API name: on/off selects7+ROM04 /4+ROMFF, retaining the charge preset.
// Saves platform and threshold after configuration readback; no enforcement claim.
std::string setBatteryAutomatic(EcAccess& ec, bool enabled);
std::string setBatteryEcPolicy(EcAccess& ec,BatteryEcPolicy);
}
