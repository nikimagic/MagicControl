#pragma once
#include "MagicControl/ResourceProvider.h"
#include <filesystem>
#include <stop_token>

namespace mc::linux_platform {
struct HwmonResourceScan {
    ResourceAvailability availability{ResourceAvailability::Unknown};
    std::string status;
    std::vector<SensorReading> readings;
};
// Reads standard *_input files, names/labels and optional temp/fan *_fault
// status. The alternate root is
// used by offline tests; production passes /sys/class/hwmon.
HwmonResourceScan scanHwmonResources(const std::filesystem::path& root,std::stop_token stop={});
class HwmonResourceProvider final:public ResourceProvider {
public:
    HwmonResourceProvider(const BoardDescriptor&,std::function<HwmonResourceScan()> scan);
    ResourceCatalog observe() override;
private:
    BoardDescriptor board_;
    std::function<HwmonResourceScan()> scan_;
};
}
