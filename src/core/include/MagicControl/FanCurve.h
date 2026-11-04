#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <algorithm>

namespace mc {
class EcAccess;
struct FanCurveCell {
    int temperature{255},duty{100};
    friend bool operator==(const FanCurveCell&,const FanCurveCell&)=default;
};
using FanCurveTable=std::array<FanCurveCell,16>;

struct FanCurvePoint {
    int temperature{};
    int duty{};
    friend bool operator==(const FanCurvePoint&, const FanCurvePoint&) = default;
};

/// Which EC table an apply operation updates. In common mode either selected
/// curve becomes the shared curve; in split mode the other table is preserved.
enum class FanCurveTarget : std::uint8_t { Cpu, Gpu, Both };

// Shared host-controlled stepped curve; protocol adapters only write the target.
inline int fanCurveLevel(std::span<const FanCurvePoint> curve,int previous,double temperature,int hysteresis=3){
    if(curve.empty())return 0;
    int level=std::clamp(previous,0,static_cast<int>(curve.size())-1);
    while(level+1<static_cast<int>(curve.size())&&temperature>=curve[level+1].temperature)++level;
    while(level>0&&temperature<=curve[level-1].temperature-hysteresis)--level;
    return level;
}
struct NativeFanState {
    std::vector<FanCurvePoint> cpu, gpu;
    bool shared{};
    bool active{};
    bool defaultMode{};
    bool boost{};
};
[[nodiscard]] NativeFanState readNativeFanState(EcAccess& access, bool readGpuTable = true);
// Same mode decoder as readNativeFanState, without reading either RAM table.
[[nodiscard]] NativeFanState readNativeFanFlags(EcAccess& access);

/// Validates the logical points used by both Windows and the future Wayland UI.
/// Temperature 255 is the EC sentinel for an unused slot and is excluded here.
[[nodiscard]] bool validateFanCurve(const std::vector<FanCurvePoint>& points,
                                    std::string& error);

// A user point may be20..100C; a device's independent thermal protection is
// applied to its encoded curve without narrowing those input-field bounds.
inline std::vector<FanCurvePoint> fanCurveWithMaximumStage(std::span<const FanCurvePoint> requested,int maximumTemperature){
    std::vector<FanCurvePoint> effective;
    for(const auto& point:requested)if(point.temperature<maximumTemperature)effective.push_back(point);
    effective.push_back({maximumTemperature,100});return effective;
}

/// Experimental autonomous EC layout: Up[16], Down[16], Duty[16].
/// Slot zero is the firmware marker, slot 15 remains the 255 sentinel.
/// Down[0] must be preserved by the writer.
/// Encodes a separate 85 C / 100% maximum stage; logical points up to100C stay
/// in settings. Rejects effective curves exceeding the available slots.
[[nodiscard]] std::array<std::uint8_t, 48> buildAutonomousFanTable(const std::vector<FanCurvePoint>& points);

/// Writes and verifies the 16-slot native RAM table. With shared=true both
/// tables receive the same points. Both linked and independent curves retain
/// EC split control; hardware common mode does not honour the RAM target.
void applyNativeFanCurve(EcAccess& access,
                         const std::vector<FanCurvePoint>& cpu,
                         const std::vector<FanCurvePoint>& gpu,
                         bool shared,
                         FanCurveTarget target = FanCurveTarget::Both);

/// Narrow native FAN cleanup; retain RAM tables and other APP consumers.
/// Explicit Wujie Default uses resetApplicationControl from ApplicationPresence.h.
void restoreNativeFanMode(EcAccess& access);

}  // namespace mc
