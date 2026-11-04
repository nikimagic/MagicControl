#pragma once
#include <windows.h>
namespace mc::gui {
// true means a verified helper is waiting for this app to exit normally.
bool showUpdateWindow(HWND owner,UINT dpi);
}
