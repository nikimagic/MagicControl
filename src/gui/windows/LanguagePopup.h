#pragma once
#include <windows.h>
namespace mc::gui {
// Language selector / credits / donations. No EC operations in this popup.
int showLanguagePopup(HWND owner, POINT anchor, UINT dpi, int selected);
int showLaptopPopup(HWND owner, POINT anchor, UINT dpi, int selected);
}
