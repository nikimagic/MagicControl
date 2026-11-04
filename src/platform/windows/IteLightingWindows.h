#pragma once
#include "MagicControl/LightingZones.h"
#include <memory>
#include "MagicControl/HardwareInventory.h"
#include <functional>
namespace mc::windows {
// Exact HYDROC admission; never selects ITE because another model has a similar
// marketing name. Existing SingleZone/Huawei backends are not replaced.
class IteLightingController {
public:
    IteLightingController();~IteLightingController();
    LightingZonesSnapshot read();
    void apply(LightingZone,const LightingConfiguration&);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
std::shared_ptr<IteLightingController> iteLightingController();
// Explicit dump helper only. Fixed80H query, no controller/watchdog, no EC
// access and no colour/brightness/welcome/flash/registry setters. Streams rows
// before each potentially blocking request so the parent retains partial data.
bool captureIteLightingEvidence(const std::function<void(const InventoryRow&)>& emit);
}
