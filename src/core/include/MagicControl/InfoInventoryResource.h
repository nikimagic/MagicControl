#pragma once
#include "HardwareInventory.h"
#include <functional>

namespace mc {
// Collect only. Cache persistence, cancellation policy and presentation are
// owned by the caller. An injected provider is for a known source or a test;
// resource routing does not establish a device identity or enable writes.
HardwareInventory collectInfoInventoryResource(std::stop_token cancellation={},const IHardwareProvider* provider=nullptr);
struct InfoInventoryResult {
    HardwareInventory inventory;
    bool saved{},cancelled{};
    std::string error;
};
// Explicit INFO refresh: collect, check cancellation, persist, return one result.
// UI adapters must not save a second time or infer success from an empty error.
InfoInventoryResult executeInfoInventoryResource(std::stop_token cancellation={},
    const std::function<HardwareInventory(std::stop_token)>& collect={},
    const std::function<bool(const HardwareInventory&,std::string&)>& save={});
}
