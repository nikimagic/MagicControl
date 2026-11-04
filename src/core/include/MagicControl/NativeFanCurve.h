#pragma once
#include "MagicControl/FanCurve.h"
#include <optional>
namespace mc {
// Native EC bands use the existing point editor: temperature is the upper
// band boundary and duty is nominal RPM percent, not hardware PWM.
// Same16-cell tables as FanCurveProfiles. Native capacity is device data,
// not a second8-point user model. Transport codecs consume only active cells.
using NativeFanCurves=std::array<FanCurveTable,2>;
struct NativeFanCurveState {
    NativeFanCurves actual{},draft{};
    std::array<std::array<int,16>,2> slopes{};
    std::array<std::optional<int>,2> temperatures;
    int selector{};
    std::array<int,2> actualPointCounts{}; // Read-only factory rows may include stop/guard records.
    bool appliedCurve{};
};
}
