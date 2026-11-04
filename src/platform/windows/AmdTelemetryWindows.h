#pragma once
#include <memory>
#include "MagicControl/Monitoring.h"
#include "MagicControl/GpuDevice.h"
namespace mc::windows {
// Read-only ADL PMLog and ADLX metrics, following the reference implementation.
class AmdTelemetryReader {
public:
    AmdTelemetryReader();
    ~AmdTelemetryReader();
    void read(MonitoringSnapshot& sample);
    void readTemperatures(MonitoringSnapshot& sample);
    // Independent ADL identities/snapshots, including APUs; no tuning setters.
    std::vector<GpuReading> readAdapters();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
