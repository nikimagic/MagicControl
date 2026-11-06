#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace mc {
// WMI TIME_CREATED is UTC FILETIME (100 ns since 1601), not steady_clock.
// These helpers only classify supplied values; they never query a provider.
inline std::optional<std::uint64_t> parseWmiUint64(std::wstring_view text)noexcept{
    if(text.empty()||text.size()>20)return {};
    std::uint64_t value{};
    for(const auto ch:text){
        if(ch<L'0'||ch>L'9')return {};
        const auto digit=static_cast<std::uint64_t>(ch-L'0');
        if(value>(std::numeric_limits<std::uint64_t>::max()-digit)/10)return {};
        value=value*10+digit;
    }
    return value;
}
using FirmwareWallClock=std::chrono::system_clock;
using FirmwareSteadyClock=std::chrono::steady_clock;
struct FirmwareEventBoundary {
    std::uint64_t filetime100ns{};
    FirmwareWallClock::time_point wall;
    FirmwareSteadyClock::time_point steady;
};
inline bool firmwareEventClockStable(FirmwareWallClock::time_point wallStart,FirmwareSteadyClock::time_point steadyStart,
        FirmwareWallClock::time_point wallEnd,FirmwareSteadyClock::time_point steadyEnd)noexcept{
    if(steadyEnd<steadyStart)return false;
    const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(wallEnd-wallStart).count();
    const auto steady=std::chrono::duration_cast<std::chrono::milliseconds>(steadyEnd-steadyStart).count();
    // Do not treat a clock adjustment as evidence of event order.
    return wall>=steady-250&&wall<=steady+250;
}
inline std::optional<std::uint64_t> firmwareEventFiletime(FirmwareWallClock::time_point wall)noexcept{
    constexpr std::int64_t offset=116444736000000000LL;
    using Ticks=std::chrono::duration<std::int64_t,std::ratio<1,10000000>>;
    const auto ticks=std::chrono::duration_cast<Ticks>(wall.time_since_epoch()).count();
    if(ticks< -offset||ticks>std::numeric_limits<std::int64_t>::max()-offset)return {};
    return static_cast<std::uint64_t>(ticks+offset);
}
inline std::optional<FirmwareEventBoundary> makeFirmwareEventBoundary(
        FirmwareWallClock::time_point wallStart,FirmwareSteadyClock::time_point steadyStart,
        FirmwareWallClock::time_point wallEnd,FirmwareSteadyClock::time_point steadyEnd)noexcept{
    if(!firmwareEventClockStable(wallStart,steadyStart,wallEnd,steadyEnd))return {};
    const auto filetime=firmwareEventFiletime(wallEnd);if(!filetime)return {};
    return FirmwareEventBoundary{*filetime,wallEnd,steadyEnd};
}
enum class FirmwareEventAge {Fresh,Stale,Unknown};
enum class FirmwareEventDisposition {Deliver,DiscardStale,ResubscribeUnknown};
inline FirmwareEventDisposition firmwareEventDisposition(FirmwareEventAge age,bool mayControl)noexcept{
    if(age==FirmwareEventAge::Stale)return FirmwareEventDisposition::DiscardStale;
    if(age==FirmwareEventAge::Unknown&&mayControl)return FirmwareEventDisposition::ResubscribeUnknown;
    return FirmwareEventDisposition::Deliver;
}
// A new subscription has no pre-capture queue. If no timestamp was observed
// before an old queue becomes empty, renew proactively instead of suppressing
// the user's first unrelated hotkey long after the capture.
class FirmwareEventQueueFilter {
    bool active_{},trustedTime_{};
public:
    void captureBoundary()noexcept{active_=true;trustedTime_=false;}
    [[nodiscard]] FirmwareEventDisposition decide(FirmwareEventAge age,bool mayControl)noexcept{
        if(!active_)return FirmwareEventDisposition::Deliver;
        if(age!=FirmwareEventAge::Unknown)trustedTime_=true;
        return firmwareEventDisposition(age,mayControl);
    }
    [[nodiscard]] bool renewAfterEmpty()const noexcept{return active_&&!trustedTime_;}
    void renewed()noexcept{active_=false;trustedTime_=false;}
    [[nodiscard]] bool active()const noexcept{return active_;}
};
inline FirmwareEventAge classifyFirmwareEvent(std::optional<std::uint64_t> eventFiletime,
        const std::optional<FirmwareEventBoundary>& boundary,FirmwareWallClock::time_point wallNow,
        FirmwareSteadyClock::time_point steadyNow)noexcept{
    if(!eventFiletime||!*eventFiletime||!boundary||
       !firmwareEventClockStable(boundary->wall,boundary->steady,wallNow,steadyNow))return FirmwareEventAge::Unknown;
    const auto now=firmwareEventFiletime(wallNow);
    if(!now||*eventFiletime>*now)return FirmwareEventAge::Unknown;
    return *eventFiletime>boundary->filetime100ns?FirmwareEventAge::Fresh:FirmwareEventAge::Stale;
}
}
