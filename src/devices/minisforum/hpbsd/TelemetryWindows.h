#pragma once
#include "MagicControl/Monitoring.h"
#include <memory>
namespace mc::windows {
// HPBSD BIOS1.06 only. Fixed I2EC getters; no fan/power configuration writes.
class HpbsdTelemetryReader {
public:
    HpbsdTelemetryReader();
    ~HpbsdTelemetryReader();
    static bool supportedHost();
    void read(MonitoringSnapshot& sample);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
