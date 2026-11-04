// MagicControl - scroll model tests.
//
// The arithmetic here was wrong once, in a way only a touchpad could reveal: a
// synthetic mouse wheel sends one message per notch, which hid the fact that a
// precision touchpad sends hundreds of small ones. These tests pin the
// behaviour for the small-delta case, and they run on any platform because the
// model itself has no operating system dependency.

#include <cstdint>
#include <cstdio>

#include "MagicControl/ScrollModel.h"
#include "TestSupport.h"

namespace mc::tests {
namespace {

/// One notch of a classic mouse moves the configured distance.
void testWholeNotch() {
    ScrollModel scroll;
    scroll.setExtent(1000);
    scroll.setViewport(200);

    scroll.accumulateWheel(-kWheelDeltaUnit, 100);
    const int moved = scroll.applyPendingWheel(100);
    CHECK(moved == 100);
    CHECK(scroll.position() == 100);
}

/// Upward movement is the negative direction, and stops at the top.
void testDirectionAndClamp() {
    ScrollModel scroll;
    scroll.setExtent(1000);
    scroll.setViewport(200);
    scroll.setPosition(50);

    scroll.accumulateWheel(kWheelDeltaUnit, 100);
    CHECK(scroll.applyPendingWheel(100) == -50);
    CHECK(scroll.position() == 0);

    // Already at the top: the movement is consumed but nothing changes.
    scroll.accumulateWheel(kWheelDeltaUnit, 100);
    CHECK(scroll.applyPendingWheel(100) == 0);
    CHECK(scroll.position() == 0);
}

/// The bottom clamp leaves the last pixel of content visible.
void testBottomClamp() {
    ScrollModel scroll;
    scroll.setExtent(500);
    scroll.setViewport(200);
    CHECK(scroll.maximumPosition() == 300);

    scroll.accumulateWheel(-kWheelDeltaUnit * 100, 100);
    static_cast<void>(scroll.applyPendingWheel(100));
    CHECK(scroll.position() == 300);
    CHECK(scroll.atEnd());
}

/// A precision touchpad sends many small deltas. Every one must move the content,
/// and the total must match the sum of the deltas.
void testSmallDeltasAccumulate() {
    ScrollModel scroll;
    scroll.setExtent(100000);
    scroll.setViewport(1000);

    // 100 messages of 20 units is 2000 units, which at 100 pixels per notch is
    // 2000 * 100 / 120 = 1666 pixels.
    for (int index = 0; index < 100; ++index) {
        scroll.accumulateWheel(-20, 100);
        static_cast<void>(scroll.applyPendingWheel(100));
    }
    const int total = scroll.position();
    // Preserve 2/3 of a pixel for the next event; consume every input once.
    CHECK(total == 1666);
    CHECK(scroll.position() == total);
}

/// Movement smaller than a pixel must be remembered, not discarded.
void testSubPixelMovementIsNotLost() {
    ScrollModel scroll;
    scroll.setExtent(10000);
    scroll.setViewport(1000);

    // One unit at 100 pixels per notch is below one pixel, so the first message
    // may legitimately move nothing, but repeated messages must add up.
    for (int index = 0; index < 120; ++index) {
        scroll.accumulateWheel(-1, 100);
        static_cast<void>(scroll.applyPendingWheel(100));
    }
    const int total = scroll.position();
    // Exact conversion: 120 units * 100 pixels / 120 = 100 pixels.
    CHECK(total == 100);
}

/// Reversing direction mid-gesture must not drag the content backwards.
void testDirectionChangeDropsRemainder() {
    ScrollModel scroll;
    scroll.setExtent(10000);
    scroll.setViewport(1000);
    scroll.setPosition(500);

    scroll.accumulateWheel(-17, 100);
    static_cast<void>(scroll.applyPendingWheel(100));
    const int down = scroll.position();
    CHECK(down >= 500);

    scroll.accumulateWheel(17, 100);
    static_cast<void>(scroll.applyPendingWheel(100));
    CHECK(scroll.position() <= down);
}

/// Page steps overlap a little so the reader does not lose their place.
void testPageStepOverlaps() {
    ScrollModel scroll;
    scroll.setExtent(10000);
    scroll.setViewport(1000);

    const int forward = scroll.pageStep(true);
    CHECK(forward > 0);
    CHECK(forward < 1000);
    CHECK(forward >= 800);

    const int backward = scroll.pageStep(false);
    CHECK(backward == -forward);
}

/// Line steps use the height the caller reports, and never return zero.
void testLineStep() {
    ScrollModel scroll;
    CHECK(scroll.lineStep(true, 20) == 20);
    CHECK(scroll.lineStep(false, 20) == -20);
    // A caller that reports a zero line height still moves by something, so an
    // arrow key is never silently ignored.
    CHECK(scroll.lineStep(true, 0) == 1);
}

/// Shrinking the extent or the viewport must pull the position back into range.
void testExtentAndViewportShrink() {
    ScrollModel scroll;
    scroll.setExtent(1000);
    scroll.setViewport(200);
    scroll.setPosition(800);
    CHECK(scroll.position() == 800);

    scroll.setExtent(300);
    CHECK(scroll.position() == 100);

    scroll.setViewport(300);
    CHECK(scroll.position() == 0);
    CHECK(!scroll.scrollable());
}

/// Content that fits does not scroll at all.
void testContentThatFits() {
    ScrollModel scroll;
    scroll.setExtent(200);
    scroll.setViewport(200);
    CHECK(!scroll.scrollable());
    scroll.accumulateWheel(-kWheelDeltaUnit * 10, 100);
    CHECK(scroll.applyPendingWheel(100) == 0);
    CHECK(scroll.position() == 0);
}

/// Reset returns to the top and forgets held-back movement.
void testReset() {
    ScrollModel scroll;
    scroll.setExtent(10000);
    scroll.setViewport(1000);
    scroll.setPosition(500);
    scroll.accumulateWheel(-5, 100);
    static_cast<void>(scroll.applyPendingWheel(100));

    scroll.reset();
    CHECK(scroll.position() == 0);
    CHECK(scroll.pendingWheelUnits() == 0);
}

/// A zero or negative pixels-per-notch setting must not divide by zero.
void testDegeneratePixelsPerNotch() {
    ScrollModel scroll;
    scroll.setExtent(10000);
    scroll.setViewport(1000);
    scroll.accumulateWheel(-120, 0);
    CHECK(scroll.applyPendingWheel(0) == 0);
    scroll.accumulateWheel(-120, -5);
    CHECK(scroll.applyPendingWheel(-5) == 0);
    CHECK(scroll.position() == 0);
}

}  // namespace

int runScrollModelTests() {
    testWholeNotch();
    testDirectionAndClamp();
    testBottomClamp();
    testSmallDeltasAccumulate();
    testSubPixelMovementIsNotLost();
    testDirectionChangeDropsRemainder();
    testPageStepOverlaps();
    testLineStep();
    testExtentAndViewportShrink();
    testContentThatFits();
    testReset();
    testDegeneratePixelsPerNotch();

    std::printf("scroll model: %d checks, %d failure(s)\n", checkCount(), failureCount());
    return failureCount();
}

}  // namespace mc::tests
