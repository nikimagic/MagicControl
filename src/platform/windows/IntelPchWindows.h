#pragma once
#include "MagicControl/Monitoring.h"
namespace mc::windows {
class IntelPchReader {
public:IntelPchReader();~IntelPchReader();void read(MonitoringSnapshot&);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
