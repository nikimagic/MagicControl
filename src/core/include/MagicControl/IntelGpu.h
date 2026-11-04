#pragma once
#include "GpuDevice.h"
namespace mc {
// Read-only Intel GPU telemetry. Includes all Intel adapters, independently of
// the primary display. No EC/MSR map, tuning setter or rendering workload.
class IntelGpu {
public:
    IntelGpu();~IntelGpu();
    std::vector<GpuReading> read();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
