#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mc {
class EcAccess;
struct NativeFanGraphPoint {
    double temperature{},duty{};
    friend bool operator==(const NativeFanGraphPoint&,const NativeFanGraphPoint&)=default;
};
struct NativeDefaultFanInput {
    std::string source;
    std::uint16_t address{};
    double temperature{};
    int stage{};
    std::vector<NativeFanGraphPoint> rising,falling;
    friend bool operator==(const NativeDefaultFanInput&,const NativeDefaultFanInput&)=default;
};
// Observed read-only Default context with a firmware-decoded base request.
// It is never a Manual draft, setter, active-table receipt or applied-PWM claim.
struct NativeDefaultFanChannel {
    std::vector<NativeFanGraphPoint> points;
    std::string profile,temperatureSource,requestSource,curveSource,graphCaption;
    std::optional<double> temperatureCelsius,requestedPercent;
    bool requestDerived{},requestPreOverride{},forced{},retained{};
    int startCelsius{48},offBelowCelsius{47};
    std::uint16_t table{},temperatureAddress{};
    std::uint64_t sampledAtMs{};
    // RPM factory views retain their real units and every contributing input.
    // They never pass through a Manual percentage curve or its storage codec.
    bool rpm{};
    std::optional<double> requestedRpm,actualRpm;
    int stage{},demandStage{};
    std::vector<NativeDefaultFanInput> inputs;
    friend bool operator==(const NativeDefaultFanChannel&,const NativeDefaultFanChannel&)=default;
};
struct NativeDefaultFans {
    std::array<NativeDefaultFanChannel,2> channels;
    std::array<double,2> actualPercent{}; // Collector ingestion; catalog duty owns presentation.
    std::array<double,2> baseInputRequests{};
    double sharedRequestedPercent{};
    friend bool operator==(const NativeDefaultFans&,const NativeDefaultFans&)=default;
};
enum class NativeDefaultFanObservation { Unavailable, Inactive, Available };
struct NativeDefaultFanReading {
    NativeDefaultFanObservation observation{NativeDefaultFanObservation::Unavailable};
    std::optional<NativeDefaultFans> fans;
    friend bool operator==(const NativeDefaultFanReading&,const NativeDefaultFanReading&)=default;
};
// A real EC-owned mode uses only the confirmed state and accepted resource.
// Local Manual draft selection is intentionally not an input to this decision.
inline bool showObservedDefaultFan(bool preview,bool confirmedDefault,bool manual,bool boost,bool online,bool pending,const NativeDefaultFanChannel* curve) noexcept {
    return !preview&&confirmedDefault&&!manual&&!boost&&!online&&!pending&&curve;
}
inline bool nativeDefaultFanFresh(const NativeDefaultFanChannel& value,std::uint64_t maxAgeMs=10000){
    const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    // Display grace must exceed the real 2-3s collector cadence. Never renew
    // sampledAtMs on a missed read; confirmed non-Default context clears at once.
    return value.sampledAtMs&&now>=value.sampledAtMs&&now-value.sampledAtMs<=maxAgeMs;
}
// Exact-board protocol observation only; no factory mailbox, activation or write.
NativeDefaultFanReading readNativeDefaultFans(EcAccess&);
}
