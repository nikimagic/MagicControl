#pragma once
#include <cstdint>

namespace mc::linux_platform {
// UI-thread owned. In-flight work is not cancelled; its result is stale after
// a sleep transition and must never replace the next post-resume observation.
class SettingsObservationEpoch {
public:
    void transition(bool asleep) noexcept { if(asleep_!=asleep){asleep_=asleep;++epoch_;} }
    bool asleep() const noexcept { return asleep_; }
    std::uint64_t capture() const noexcept { return epoch_; }
    bool current(std::uint64_t stamp) const noexcept { return !asleep_&&stamp==epoch_; }
private:
    std::uint64_t epoch_{};
    bool asleep_{};
};
}
