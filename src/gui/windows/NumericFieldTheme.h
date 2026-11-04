#pragma once

#include <windows.h>
#include "Geometry.h"

namespace mc::gui {

// Styles existing EDIT + UPDOWN windows. Windows keeps ownership of numeric
// input, accessibility, buddy synchronization, keyboard and button repetition.
[[nodiscard]] bool applyNumericFieldTheme(HWND edit, HWND upDown) noexcept;
struct NumericFieldWindows {HWND edit{},spin{};};
// Shared native builder, using the same edit/spin theme and buddy mechanics.
NumericFieldWindows createNumericFieldWindows(HWND parent,int editId,int spinId,int minimum,int maximum,int value,int textLimit=4,bool hexadecimal=false);
// Size belongs to each consumer: FANs, power and timer fields need not be equal.
// Pass the page font explicitly when positioning fields outside its place()
// wrapper. nullptr preserves an existing consumer font (e.g. FANs Caption).
void placeNumericFieldWindows(NumericFieldWindows field,ui::Rect bounds,int arrowWidth,int offsetY=0,HFONT font=nullptr);
// Opt-in page focus policy: Enter consumes the key; pointer events keep their
// original destination after releasing focus outside the current numeric pair.
bool releaseNumericFieldFocusWindows(const MSG& message,HWND focusTarget);

// Returns nullptr for a control outside applyNumericFieldTheme.
[[nodiscard]] HBRUSH numericFieldColors(HDC dc, HWND control) noexcept;

}  // namespace mc::gui
