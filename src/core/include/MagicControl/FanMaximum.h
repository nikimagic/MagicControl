#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>
namespace mc {
struct FanMaximumSample {
    std::uint64_t elapsedMs{};
    std::array<std::optional<int>,2> rpm;
    std::array<std::optional<double>,2> percent;
};
struct FanMaximumResult {
    std::string source,status;
    // Accepted command, stable RPM plateau and measured rotor response are
    // independent evidence. A plateau alone never proves execution of Max.
    bool attempted{},commandApplied{},responseObserved{},settled{},restored{true};
    std::array<std::optional<int>,2> beforeRpm,peakRpm;
    std::vector<FanMaximumSample> samples;
};
using MaximumFanProbe=std::function<FanMaximumResult(std::stop_token)>;
// One bounded sampling/cleanup policy for EC and NVML. Callbacks must capture
// the original control state before any write and confirm its restoration.
FanMaximumResult measureMaximumFans(std::string source,const std::function<void()>& begin,
    const std::function<FanMaximumSample()>& read,const std::function<void()>& restore,std::stop_token token);
}
