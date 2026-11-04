#pragma once
#include <array>
#include <memory>
#include <string>
#include "MagicControl/FanMaximum.h"
#include "MagicControl/DiagnosticDump.h"
#include "MagicControl/NativeDefaultFan.h"
namespace mc::windows {
enum class HlylAutoResult { Applied, AlreadyAuto, OwnershipLost };
struct HlylFanState {
    bool manual{},writable{};
    std::array<int,2> actualPercent{},targetPercent{};
    std::string status;
    NativeDefaultFanReading defaultFans;
};
// HLYL BIOS1.09 only. Owns an explicit manual request, never applies on startup.
// While owned, a watchdog returns firmware automatic at80C or lost telemetry.
// Destruction releases OUR manual request; it does not change MECHREVO policy.
class HlylFanController : public std::enable_shared_from_this<HlylFanController> {
public:
    HlylFanController();
    ~HlylFanController();
    HlylFanState read();
    void setFixed(const std::array<int,2>& percent);
    HlylAutoResult setAutomatic();
    // Explicit diagnostic action; uses the common30-second sampler and owns
    // one DeviceOperation reservation. It never invents an RPM target.
    FanMaximumResult measureMaximum(std::stop_token token);
    // Capture only our live fixed request, select Default, then offer a guarded
    // one-shot restoration after diagnostic collection. Never load CSV drafts.
    FanDiagnosticSession prepareDiagnostic();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// The GUI and diagnostics must share ownership/watchdog lifetime.
std::shared_ptr<HlylFanController> hlylFanController();
}
