#pragma once
#include <memory>
#include "MagicControl/Monitoring.h"
namespace mc::windows {
ProcessorVendor nativeCpuVendor() noexcept;
class IntelTelemetryReader {
public:
    explicit IntelTelemetryReader(bool powerDiagnostics=false); ~IntelTelemetryReader();
    void read(MonitoringSnapshot& sample);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
