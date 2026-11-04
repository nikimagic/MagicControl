#pragma once
#include <cstdint>
#include <optional>

namespace mc::windows {
struct FanPowerTime { std::uint64_t elapsedMs{}, awakeMs{}; };
class FanSleepTracker {
public:
    bool observe(FanPowerTime current) {
        const auto previous = previous_;
        previous_ = current;
        if (!previous || current.elapsedMs < previous->elapsedMs || current.awakeMs < previous->awakeMs) return false;
        const auto elapsed = current.elapsedMs - previous->elapsedMs;
        const auto awake = current.awakeMs - previous->awakeMs;
        // A long EC/SDK call advances both clocks. Only suspended system time
        // advances the elapsed clock without advancing the unbiased clock.
        return elapsed > awake && elapsed - awake >= 1000;
    }
private:
    std::optional<FanPowerTime> previous_;
};
}
