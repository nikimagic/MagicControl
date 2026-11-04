#pragma once
#include "MagicControl/GpuDevice.h"
namespace mc::windows {
struct NvidiaPciIdentity {unsigned domain{},bus{},device{},deviceId{},subsystemId{};};
struct NvidiaPowerObservation {
    unsigned bus{},device{},function{};std::optional<unsigned> state;
    std::string instance,status;std::uint64_t sampledAt{};
};
// PnP last reported D-state, not an MUX mode and not proof of D3cold/runtime D3.
// Does not initialize NVAPI/NVML or create a GPU workload.
std::vector<NvidiaPowerObservation> nvidiaPowerObservations();
// Read-only supplement to NVML, matched by PCI identity rather than GPU name/index.
class NvidiaSensors {
public:
    NvidiaSensors();~NvidiaSensors();
    void read(GpuReading&,const NvidiaPciIdentity&);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
