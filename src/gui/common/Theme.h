// MagicControl - shared GUI palette.
//
// Colours are taken from the reference screenshots of the C# application. The
// scheme is a dark navy surface with a violet accent.
//
// Every value carries a confidence marker:
//   [read]  sampled directly from a screenshot
//   [fit]   approximated to match the perceived colour, to be corrected against
//           the C# source (Form1.Set.cs / UiText.cs) before the GUI is finished
#pragma once

#include <cstdint>

namespace mc::gui {

/// A colour in 0x00BBGGRR order, which is what Win32 COLORREF and GDI expect.
using ColorRef = std::uint32_t;

/// Builds a COLORREF from familiar red/green/blue components.
[[nodiscard]] constexpr ColorRef rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept {
    return static_cast<ColorRef>(red) | (static_cast<ColorRef>(green) << 8) |
           (static_cast<ColorRef>(blue) << 16);
}

namespace theme {
inline constexpr ColorRef kMonitorSurface = rgb(23,22,43);
inline constexpr ColorRef kFanNestedSurface = rgb(30,29,52);
inline constexpr ColorRef kFanValueText = rgb(249,248,255);
inline constexpr ColorRef kMonitorText = rgb(166,163,199);
inline constexpr ColorRef kMonitorSource = rgb(154,151,181);
inline constexpr ColorRef kBatteryCharging = rgb(90,210,140);
inline constexpr ColorRef kBatteryDischarging = rgb(245,105,105);

// --- Surfaces --------------------------------------------------------------
/// Window and page background. Form1.cs: BackColor = Color.FromArgb(11, 11, 21).
inline constexpr ColorRef kBackground = rgb(0x0B, 0x0B, 0x15);  // [read]
/// BIOS inventory card surface. BiosInventoryView.OnPaint fills cards with
/// Color.FromArgb(23, 22, 43).
inline constexpr ColorRef kSurface = rgb(0x17, 0x16, 0x2B);     // [read]
/// Slightly lighter surface for nested blocks and inputs.
inline constexpr ColorRef kSurfaceRaised = rgb(0x24, 0x28, 0x47);  // [fit]

// Numeric fields: the same palette as the GTK controls in the Linux frontend.
inline constexpr ColorRef kNumericBackground = rgb(0x24, 0x25, 0x41);
inline constexpr ColorRef kNumericBorder = rgb(0x41, 0x42, 0x63);
inline constexpr ColorRef kNumericButtonBorder = rgb(0x30, 0x31, 0x4E);
inline constexpr ColorRef kNumericText = rgb(0xF3, 0xF2, 0xFA);
inline constexpr ColorRef kNumericHover = rgb(0x2D, 0x2E, 0x50);
inline constexpr ColorRef kNumericPressed = rgb(0x60, 0x4A, 0xC0);
inline constexpr ColorRef kNumericDisabledBackground = rgb(0x1A, 0x1A, 0x2B);
inline constexpr ColorRef kNumericDisabledText = rgb(0x85, 0x89, 0xAD);

// --- Title bar -------------------------------------------------------------
// Dark, matching the pages. The reference's light strip was rejected by the user.
inline constexpr ColorRef kTitleBar = rgb(0x11, 0x13, 0x24);  // [fit]

/// Scroll bar thumb on a dark page, from PageScrollBar's dark palette:
/// Color.FromArgb(104, 99, 139) at rest, (139, 124, 204) hovered or focused,
/// (151, 127, 255) while dragged.
inline constexpr ColorRef kScrollThumb = rgb(0x68, 0x63, 0x8B);          // [read]
inline constexpr ColorRef kScrollThumbHot = rgb(0x8B, 0x7C, 0xCC);       // [read]
inline constexpr ColorRef kScrollThumbDragging = rgb(0x97, 0x7F, 0xFF);  // [read]

// --- Text ------------------------------------------------------------------
/// Primary text, large readouts and headings. Form1.cs: ForeColor =
/// Color.FromArgb(244, 243, 255).
inline constexpr ColorRef kTextPrimary = rgb(0xF4, 0xF3, 0xFF);  // [read]
/// Inventory caption text. BiosInventoryView.OnPaint draws captions with
/// Color.FromArgb(175, 172, 211).
inline constexpr ColorRef kTextSecondary = rgb(0xAF, 0xAC, 0xD3);  // [read]
/// Muted text: disabled entries and hints.
inline constexpr ColorRef kTextMuted = rgb(0x7A, 0x7F, 0x9C);  // [read]

// --- Accent ----------------------------------------------------------------
/// Accent used for the active tab underline. CaptionTabButton.OnPaint fills it
/// with Color.FromArgb(96, 76, 198).
inline constexpr ColorRef kAccent = rgb(0x60, 0x4C, 0xC6);  // [read]
/// Accent under the mouse.
inline constexpr ColorRef kAccentHover = rgb(0x7E, 0x70, 0xEC);
/// Accent while pressed.
inline constexpr ColorRef kAccentPressed = rgb(0x5A, 0x4B, 0xD4);
/// Cyan used for the GPU curve.
inline constexpr ColorRef kAccentSecondary = rgb(0x4F, 0xC3, 0xF7);  // [read]

// --- Lines and states ------------------------------------------------------
inline constexpr ColorRef kDivider = rgb(0x2C, 0x30, 0x52);
/// Overlay for a hovered control on a dark surface.
inline constexpr ColorRef kHoverOverlay = rgb(0x2A, 0x2E, 0x50);
/// Text always drawn dark on the light caption strip.
inline constexpr ColorRef kTitleBarTextSecondary = rgb(0x53, 0x50, 0x61);
inline constexpr ColorRef kCloseHover = rgb(0xC4, 0x2B, 0x1C);
inline constexpr ColorRef kOk = rgb(0x4C, 0xD1, 0x64);
inline constexpr ColorRef kWarning = rgb(0xE8, 0xB3, 0x39);
inline constexpr ColorRef kError = rgb(0xE8, 0x5B, 0x5B);

}  // namespace theme
}  // namespace mc::gui
