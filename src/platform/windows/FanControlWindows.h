#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <future>
#include <functional>
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/Performance.h"
#include "MagicControl/PortableSettings.h"
#include "FanPowerTime.h"
#include "MagicControl/FanMaximum.h"
#include "MagicControl/DiagnosticDump.h"

namespace mc { class EcAccess; }
namespace mc::windows {
struct FanControlStatus {
    std::uint64_t revision{};
    // Read-only EC observation; revision counts commands, not mode detection.
    // Empty during startup/resume/read failure, false for mixed or active flags.
    std::optional<bool> observedDefault;
    bool ecManual{}, busy{}, applying{}, needsRestore{}, error{}, owned{}, maximum{}, linked{}, suspended{};
    std::array<bool, 2> controlled{}, confirmed{};
    std::array<FanCurveTable, 2> applied{};
    std::array<std::optional<int>, 2> target{}, rpm{};
    std::array<std::optional<double>, 2> actual{};
    std::string message;
};

class FanOwnership {
public:
    virtual ~FanOwnership() = default;
    virtual void restoreService() = 0;
};
class FanControlEnvironment {
public:
    virtual ~FanControlEnvironment() = default;
    virtual MonitoringSnapshot readTemperatures() = 0;
    virtual std::unique_ptr<FanOwnership> acquireOwnership(bool pauseService) = 0;
    virtual std::unique_ptr<FanOwnership> acquireStandaloneOwnership() { return acquireOwnership(false); }
    virtual FanPowerTime powerTime() const noexcept;
};

// CLI selection requires ownership of the GUI instance and both fan channels.
PerformanceChange selectStandalonePerformance(EcAccess& access, int preset, FanControlEnvironment* environment = nullptr);
PerformanceChange selectStandaloneCustomPower(EcAccess&,const std::array<int,3>&,FanControlEnvironment* environment=nullptr);
PerformanceChange selectStandaloneCustomBase(EcAccess&,int,FanControlEnvironment* environment=nullptr);

// The configuration restorer may replay a validated saved selection after a lifecycle event. The worker
// retains exclusive fan ownership and reads its own fresh AMD temperatures.
class FanController {
public:
    explicit FanController(EcAccess& access, std::unique_ptr<FanControlEnvironment> environment = {});
    ~FanController();
    FanController(const FanController&) = delete;
    FanController& operator=(const FanController&) = delete;
    void apply(int channel, const FanCurveTable& curve, bool linked);
    void restoreSavedProfiles(const FanCurveProfiles& profiles, std::shared_ptr<DeviceOperation> operation = {}, bool adoptExisting = false);
    void ecManual(const FanCurveTable& cpu, const FanCurveTable& gpu,std::shared_ptr<DeviceOperation> operation={});
    std::future<void> ecManualAndWait(const FanCurveTable&,std::shared_ptr<DeviceOperation>);
    void native(std::shared_ptr<DeviceOperation> operation={},bool resetApplicationConsumers=true);
    void maximum(std::shared_ptr<DeviceOperation> operation={});
    void link(bool linked);
    void restoreApplicationPresence();
    void suspendPower();
    void resumePower(bool sourceChanged = false);
    void sessionDisplayChanged(unsigned state);
    void setConfigurationRestoreNotifier(std::function<void()> notifier);
    void keyboardActivity();
    std::future<PerformanceChange> performance(int preset, std::shared_ptr<DeviceOperation> operation = {});
    std::future<PerformanceChange> customPower(const std::array<int,3>&,std::shared_ptr<DeviceOperation> operation = {});
    std::future<PerformanceChange> customBase(int,std::shared_ptr<DeviceOperation> operation = {});
    std::future<void> nativeAndWait(std::shared_ptr<DeviceOperation> operation = {},bool resetApplicationConsumers = true);
    // Captures the worker's confirmed intent, never the CSV editor draft.
    // Its one-shot restore uses the same worker and fresh Default admission.
    FanDiagnosticSession prepareDiagnostic();
    std::future<FanMaximumResult> measureMaximum(std::stop_token cancellation={});
    [[nodiscard]] FanControlStatus status() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
