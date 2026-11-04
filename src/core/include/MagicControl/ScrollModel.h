// MagicControl - scroll position model.
//
// Scrolling itself is arithmetic: an extent, a viewport, a position, and a
// stream of wheel deltas that must be turned into movement. Only the finishing
// step - moving pixels on screen - belongs to the platform.
//
// Keeping the arithmetic here has three consequences that matter:
//   * arithmetic is platform-independent, rather than copied into each view;
//   * it is covered by unit tests, so the awkward parts (fractional touchpad
//     deltas, clamping, page steps) are verified without a screen;
//   * a platform layer only has to answer "shift the content by N pixels and
//     repaint the strip that was exposed".
#pragma once

#include <cstdint>

namespace mc {

// OWNER CONTRACT,27.10.2026: one project scroll mechanism, independent state per
// viewport. Windows consumers use PageMechanics for thumb/wheel/press/drag;
// Linux consumers use the shared scrolledWidget/GTK adapter. Native control
// bindings may translate units/coordinates, never duplicate the mechanism or
// change geometry while removing a duplicate.

/// Wheel delta that represents one detent on a classic mouse. A precision
/// touchpad reports fractions of this, which is why deltas are accumulated
/// instead of being rounded to whole notches.
inline constexpr int kWheelDeltaUnit = 120;

/// A vertical scroll position over a piece of content.
///
/// All distances use the same pixel unit supplied by the caller; scale once at the UI boundary.
class ScrollModel {
public:
    /// Sets the full height of the content, in logical pixels.
    void setExtent(int extent) noexcept;

    /// Sets the height of the visible area, in logical pixels.
    void setViewport(int viewport) noexcept;

    /// Moves to an absolute position, clamped to the valid range.
    void setPosition(int position) noexcept;

    [[nodiscard]] int position() const noexcept { return position_; }
    [[nodiscard]] int extent() const noexcept { return extent_; }
    [[nodiscard]] int viewport() const noexcept { return viewport_; }

    /// Highest valid position: where the last pixel of content becomes visible.
    [[nodiscard]] int maximumPosition() const noexcept;

    /// True when the content is taller than the viewport.
    [[nodiscard]] bool scrollable() const noexcept { return maximumPosition() > 0; }

    /// True when the visible area reaches the end of the content.
    [[nodiscard]] bool atEnd() const noexcept { return position_ >= maximumPosition(); }

    /// Feeds raw wheel movement, in WHEEL_DELTA units, positive away from the
    /// user.
    ///
    /// This only records the movement; nothing is applied and the position does
    /// not change. A caller feeds every message it receives and then calls
    /// applyPendingWheel once per frame, which is what keeps a touchpad, whose
    /// messages arrive in bursts of hundreds, from causing a repaint per message.
    void accumulateWheel(int wheelDelta, int pixelsPerNotch) noexcept;

    /// Converts the accumulated movement into pixels and moves the position.
    /// Returns the distance travelled, which may be zero.
    ///
    /// A caller invokes this once per frame. Movement smaller than a pixel stays
    /// in the accumulator for the next frame rather than being lost.
    [[nodiscard]] int applyPendingWheel(int pixelsPerNotch) noexcept;

    /// Feeds a page step, as produced by Page Up and Page Down. Returns the pixel
    /// movement, leaving a small overlap so the reader keeps their place.
    [[nodiscard]] int pageStep(bool forward) const noexcept;

    /// Feeds a line step, as produced by the arrow keys.
    [[nodiscard]] int lineStep(bool forward, int lineHeight) const noexcept;

    /// Raw movement waiting for the next frame (subpixel fractions are separate).
    [[nodiscard]] std::int64_t pendingWheelUnits() const noexcept { return wheelRemainder_; }

    void reset() noexcept;

private:
    /// Applies a pixel movement and returns the amount actually travelled, which
    /// is smaller at either end of the content.
    int moveBy(int pixels) noexcept;

    int extent_{0};
    int viewport_{0};
    int position_{0};
    std::int64_t wheelRemainder_{0};
    std::int64_t pixelFraction_{0};
};

}  // namespace mc
