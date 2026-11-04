#pragma once
#include "ResourceControl.h"
#include <optional>
#include <functional>
#include <string_view>

namespace mc {
// The provider's existing exact identifier is carried through unchanged.
// A label or PCI vendor alone never selects or admits a GPU write.
inline ResourceControlKey gpuPowerResourceControlKey(std::string_view providerId,bool reset){
    if(providerId.empty())throw std::invalid_argument("GPU: missing provider identity");
    return {ResourceKind::Graphics,std::string(providerId),reset?"power.reset":"power.set"};
}
struct GpuPowerResourceActions {
    std::function<void(const std::string&,int)> set;
    std::function<void(const std::string&)> reset;
};
// Calls only the selected typed backend; the backend owns range validation,
// target identity checks and readback. No catalogue observation grants write.
inline void applyGpuPowerResource(std::string_view providerId,std::optional<int> watts,bool reset,
                                  const GpuPowerResourceActions& actions){
    const auto key=gpuPowerResourceControlKey(providerId,reset);
    if(!reset&&!watts)throw std::invalid_argument("GPU: не задан лимит мощности");
    if(reset)actions.reset(key.instance);else actions.set(key.instance,*watts);
}
}
