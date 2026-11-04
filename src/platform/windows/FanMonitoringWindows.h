#pragma once
#include "MagicControl/FanTelemetry.h"
#include "MagicControl/Monitoring.h"
#include "NativeFixedFans.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include <cstdint>
#include <functional>
#include <optional>

namespace mc::windows {
struct FanSensorSnapshot {
    std::uint64_t typedGeneration{};
    bool typedObservationSkipped{};
    std::optional<Gm6State> gm6;
    std::optional<NativeFixedFanState> hlyl;
    FanReadout cpu,gpu;
    MonitoringSnapshot monitoring;
    std::optional<int> ecTemperature;
};
struct FanMonitoringContext {
    EcAccess* ec{};
    MonitoringReader& reader;
    const std::function<MonitoringSnapshot()>& source;
    NativeFixedFans* nativeFans{};
    Gm6Controller* gm6{};
    bool hpbsd{},readGmTables{};
    std::uint64_t generation{};
};
// Runs on the core executor, owned by FansOperationsWindows. No fan writes.
FanSensorSnapshot readFanMonitoring(const FanMonitoringContext&);
}
