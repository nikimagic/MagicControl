#pragma once
#include <optional>
#include <cmath>
#include "Monitoring.h"
#include "ResourceCatalog.h"
#include "NativeDefaultFan.h"
namespace mc {
class EcAccess;
struct FanReadout {
    bool rpmAvailable{}, pwmAvailable{}, temperatureAvailable{};
    int rpm{};
    double dutyPercent{};
    double temperatureCelsius{};
};
// Linux fan values originate in the same monitoring snapshot as the catalog.
// Windows can use a separately sampled EC channel and must not substitute it
// with another physical channel. Accepted resource state owns display values.
inline FanReadout monitoringFanReading(const MonitoringSnapshot& sample,int channel,
                                       const ResourcePresentationView* resources) {
    const auto& p=sample.processors[channel];
    const auto rpm=resourceDisplayValue(resources,ResourceKind::Fan,static_cast<unsigned>(channel),"speed");
    const auto pwm=resourceDisplayValue(resources,ResourceKind::Fan,static_cast<unsigned>(channel),"duty");
    FanReadout result;
    result.rpmAvailable=rpm.has_value();result.rpm=static_cast<int>(std::lround(rpm.value_or(0)));
    result.pwmAvailable=pwm.has_value();result.dutyPercent=pwm.value_or(0);
    result.temperatureAvailable=p.celsius.has_value();result.temperatureCelsius=p.celsius.value_or(0);
    return result;
}
struct FanTelemetry {
    FanReadout cpu, gpu;
    std::optional<int> ecTemperatureCelsius;
    std::optional<NativeDefaultFans> nativeDefaultFans;
    NativeDefaultFanObservation nativeDefaultFanObservation{NativeDefaultFanObservation::Unavailable};
};
/// A read-only sample for either desktop UI. Scheduling belongs to the caller.
/// EC 0x43E is a diagnostic reading, not an independent CPU/GPU die sensor.
/// The platform monitoring reader supplies the temperatures used by the UI.
[[nodiscard]] FanTelemetry readFanTelemetry(EcAccess& access);
}
