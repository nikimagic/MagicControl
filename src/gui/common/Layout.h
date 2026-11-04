#pragma once
#include "TabLabels.h"

// Shared logical dimensions at 96 DPI. The current Windows MagicControl is
// the reference; Geometry.h preserves its rounding and placement formulas.
namespace mc::gui {

inline constexpr int kTitleBarHeight = 30;
inline constexpr int kPageTopPadding = 8;
inline constexpr int kWindowButtonWidth = 46;
inline constexpr int kHeaderIconLeft = 10;
inline constexpr int kHeaderIconSize = 20;
inline constexpr int kBrandPaddingLeft = kHeaderIconLeft + kHeaderIconSize;
inline constexpr int kBrandWidth = 120;
// Target outside width. Win32 adds a 16 px owner-drawn menu gutter; GTK adds
// the one-pixel border on each side. Both frontends subtract their own chrome
// from this single shared width before requesting the inner item width.
inline constexpr int kLanguageMenuWidth = 196;
inline constexpr int kLanguageMenuWindowsGutterWidth = 16;
inline constexpr int kLanguageMenuRowHeight = 28;
inline constexpr int kLanguageMenuBorderWidth = 1;
inline constexpr int kTabUnderlineHeight = 2;
inline constexpr int kTabPaddingX = 10;

inline constexpr int kDefaultWidth = 800;
inline constexpr int kDefaultHeight = 600;
// Windows adjusts its native frame before removing the non-client area.
// Retain that observed outer size in GTK rather than changing Windows.
inline constexpr int kDefaultOuterWidth = kDefaultWidth + 13;
inline constexpr int kDefaultOuterHeight = kDefaultHeight + 36;
// Windows applies these minimum tracking dimensions in device pixels.
inline constexpr int kMinWidth = 720;
inline constexpr int kMinHeight = 560;
inline constexpr int kResizeBorder = 6;
inline constexpr int kStatusBarHeight = 30;

// Page/card reference in logical DIP, scaled exactly once for the current DPI:
// each OUTER card starts12 from the viewport left and ends12 before its right.
// The12-DIP scrollbar overlays that existing right edge lane; add NO extra gap.
// SETs content adds one inner12-DIP inset inside each outer card. A nested card
// may have its own inner padding; never apply that inset to its OUTER parent.
inline constexpr int kContentPadding = 12;
inline constexpr int kCardGap = 6; // Vertical distance between OUTER cards.
inline constexpr int kCardPadding = 10;
inline constexpr int kFanCardHeight = 164;
inline constexpr int kFanMetricRowTop = 30;
inline constexpr int kFanMetricRowHeight = 32;
inline constexpr int kFanCaptionTop = 66;
inline constexpr int kMonitorTileHeight = 58;
inline constexpr int kMonitorWrappedHeight = 74;
inline constexpr int kMonitorInlineMinWidth = 126;
inline constexpr int kFanGraphPlotOriginalHeight = 93;
inline constexpr int kFanGraphPlotHeightPercent = 80;
inline constexpr int kFanGraphPlotTop = 24;
inline constexpr int kFanGraphFooterHeight = 43;
inline constexpr int kFanEditorHeight = 138;
inline constexpr int kBigValueHeight = 42;
inline constexpr int kUnitHeight = 15;
inline constexpr int kButtonHeight = 30;
inline constexpr int kKeyboardTimeoutControlSize = 22;
inline constexpr int kKeyboardTimeoutCheckBorder = 1;
inline constexpr int kActionButtonHeight = kButtonHeight;
inline constexpr int kNativeButtonWidth = 180;
inline constexpr int kNativeButtonHeight = kButtonHeight;
inline constexpr int kCurveNumericHeight = 22;
inline constexpr int kCurveArrowWidth = 14;
inline constexpr int kInventoryColumnGap = 12;
inline constexpr int kInventoryRowGap = 3;

// OWNER CONTRACT,24.10.2026: DO NOT move/narrow cards to make empty space for
// a scrollbar. No extra gutter/padding or second subtraction of lane width.
// Applies to every tab on Windows/Linux, including the outer SETs Logger card.
// Preserve these dimensions and the working scroll behavior during other work.
// Scrollbars overlay a 12 px edge lane; they do not narrow the cards.
inline constexpr int kScrollBarTrackWidth = 12;
inline constexpr int kScrollBarTrackInset = 4;
inline constexpr int kScrollBarThumbWidth = 5;
inline constexpr int kScrollBarThumbWidthHot = 7;
inline constexpr int kScrollBarThumbMinHeight = 28;

enum class Tab : int { Fans = 0, Sets = 1, Keys = 2, Bios = 3, Nvidia = 4, Radeon = 5, OvClock = 6 };
inline constexpr int kTabCount = 7;
inline constexpr auto& kTabTitles = ui::kHeaderTabTitlesWindows;
static_assert(kTabTitles.size()==kTabCount);

}  // namespace mc::gui
