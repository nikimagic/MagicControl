#include "MagicControl/ScrollModel.h"

#include <algorithm>

namespace mc {
namespace {

/// Overlap left visible by a page step, so the reader keeps their place.
constexpr double kPageOverlapFraction = 0.1;

}  // namespace

void ScrollModel::setExtent(int extent) noexcept {
    extent_ = std::max(0, extent);
    position_ = std::clamp(position_, 0, maximumPosition());
}

void ScrollModel::setViewport(int viewport) noexcept {
    viewport_ = std::max(0, viewport);
    position_ = std::clamp(position_, 0, maximumPosition());
}

void ScrollModel::setPosition(int position) noexcept {
    position_ = std::clamp(position, 0, maximumPosition());
}

int ScrollModel::maximumPosition() const noexcept {
    return std::max(0, extent_ - viewport_);
}

int ScrollModel::moveBy(int pixels) noexcept {
    const int before = position_;
    position_ = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(position_) + pixels, 0, maximumPosition()));
    return position_ - before;
}

void ScrollModel::accumulateWheel(int wheelDelta, int pixelsPerNotch) noexcept {
    static_cast<void>(pixelsPerNotch);

    // Movement is only recorded here; it is converted and applied by
    // applyPendingWheel, once per frame.
    //
    // Keeping the pending movement in wheel units, rather than converting on
    // arrival, is what makes a precision touchpad behave. It streams hundreds of
    // values per gesture, and converting each one separately quantised the
    // position to whole pixels hundreds of times, so the content drifted away
    // from the finger.
    wheelRemainder_ -= wheelDelta;
}

int ScrollModel::applyPendingWheel(int pixelsPerNotch) noexcept {
    if (pixelsPerNotch <= 0 || wheelRemainder_ == 0) return 0;
    // Keep the exact fraction in pixel/120 units. Converting pixels back to
    // wheel units introduces a second rounding and replays consumed movement.
    const auto numerator = wheelRemainder_ * pixelsPerNotch + pixelFraction_;
    wheelRemainder_ = 0;
    const auto requested = numerator / kWheelDeltaUnit;
    pixelFraction_ = numerator % kWheelDeltaUnit;
    const int before = position_;
    position_ = static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(position_) + requested, 0, maximumPosition()));
    if (position_ - before != requested || (position_ == 0 && numerator < 0) ||
        (position_ == maximumPosition() && numerator > 0)) pixelFraction_ = 0;
    return position_ - before;
}

int ScrollModel::pageStep(bool forward) const noexcept {
    const int overlap = static_cast<int>(static_cast<double>(viewport_) * kPageOverlapFraction);
    const int step = std::max(1, viewport_ - overlap);
    return forward ? step : -step;
}

int ScrollModel::lineStep(bool forward, int lineHeight) const noexcept {
    const int step = std::max(1, lineHeight);
    return forward ? step : -step;
}

void ScrollModel::reset() noexcept {
    position_ = 0;
    wheelRemainder_ = 0;
    pixelFraction_ = 0;
}

}  // namespace mc
