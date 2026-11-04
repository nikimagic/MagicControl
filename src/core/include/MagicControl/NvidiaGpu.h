#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "FanMaximum.h"
#include "GpuDevice.h"
namespace mc {
struct MonitoringSnapshot;
using NvidiaFanReading=GpuFanReading;
using NvidiaGpuReading=GpuReading;
// OS enumeration only, no NVML initialization, GPU workload or vendor process.
bool nvidiaGpuPresent() noexcept;
class NvidiaGpu {
public:
    NvidiaGpu();~NvidiaGpu();
    std::vector<NvidiaGpuReading> read(bool fanDiagnostics=false);
    FanMaximumResult measureMaximumFans(const std::string& uuid,const std::function<FanMaximumSample()>& readPhysicalFans,std::stop_token token);
    // Explicit user requests only; re-resolve the UUID and re-read bounds.
    // Successful readback confirms the driver setting, not measured load power.
    void setPowerLimit(const std::string& uuid,unsigned milliwatts);
    void resetPowerLimit(const std::string& uuid);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
void readNvidiaMonitoring(NvidiaGpu& reader, MonitoringSnapshot& sample);
// Reuse one vendor poll for the fixed cooling source AND separate display cards.
void readNvidiaMonitoring(const std::vector<NvidiaGpuReading>& readings, MonitoringSnapshot& sample);

}
