#pragma once
#include "MagicControl/Monitoring.h"
namespace mc::windows {
class IntelMemoryReader {
public:
    IntelMemoryReader();~IntelMemoryReader();
    void read(MonitoringSnapshot& sample);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
