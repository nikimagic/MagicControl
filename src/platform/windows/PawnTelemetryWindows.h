#pragma once
#include <memory>
#include "MagicControl/Monitoring.h"
namespace mc::windows {
// AMD Zen telemetry is a CPU capability, independent of the board profile.
bool pawnAmdCpuSupported() noexcept;
class PawnTelemetryReader {
public:
    explicit PawnTelemetryReader(bool boardVerified);
    PawnTelemetryReader(bool cpuAllowed,bool smbusAllowed,bool legacyMemoryOrder=false);
    ~PawnTelemetryReader();
    void read(MonitoringSnapshot& sample);
    // Same CPU sensor used by the dashboard and the independent fan controller.
    void readTemperatures(MonitoringSnapshot& sample);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
