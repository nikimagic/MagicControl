#include "MagicControl/Logging.h"
#include "MagicControl/DeviceOperation.h"
#include "../src/gui/windows/FansPage.h"
#include "../src/gui/windows/Layout.h"
#include <cstdio>

namespace {
BOOL CALLBACK findDefault(HWND control, LPARAM data) {
    wchar_t caption[64]{};
    GetWindowTextW(control, caption, 64);
    if (wcscmp(caption, L"Default") == 0) {
        *reinterpret_cast<HWND*>(data) = control;
        return FALSE;
    }
    return TRUE;
}
}
int main() {
    mc::setLoggingEnabled(false);
    const HWND parent = CreateWindowExW(0, L"STATIC", L"FANs DPI test", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, nullptr, nullptr);
    if (!parent) return 1;
    int failures = 0;
    {
        mc::gui::FansPage page(parent, nullptr);
        HWND button{};
        EnumChildWindows(parent, findDefault, reinterpret_cast<LPARAM>(&button));
        if (!button) return 2;
        const RECT area{0, 0, 1600, 1200};
        // Startup on SETs: FANs is arranged at the new DPI while still hidden.
        // The next visible arrangement has the same DPI and cannot repair it.
        for (const UINT dpi : {192u, 144u, 192u, 0u}) {
            page.arrange(area, dpi, false);
            LOGFONTW font{};
            const auto handle = reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0));
            if (!handle || !GetObjectW(handle, sizeof(font), &font)) return 3;
            const int expected = -mc::gui::ui::fontHeight(mc::gui::ui::FontRole::Caption, dpi ? dpi : 96);
            std::printf("Hidden FANs dpi=%u caption_height=%ld expected=%d\n", dpi, font.lfHeight, expected);
            if (font.lfHeight != expected) ++failures;
        }
        // This page has no injected EC backend. Its baseline depends on the
        // actual/preview profile and must not be assumed writable. The gate
        // must disable a button, then restore exactly that prior availability.
        page.refreshCommandAvailability();
        const bool baselineEnabled=IsWindowEnabled(button)!=FALSE;
        std::printf("Default button baseline without injected EC backend: %s\n",baselineEnabled?"enabled":"disabled");
        auto operation = mc::deviceOperations().tryBegin();
        if(!operation) return 5;
        page.refreshCommandAvailability();
        if (IsWindowEnabled(button)) ++failures;
        operation.reset();
        page.refreshCommandAvailability();
        if ((IsWindowEnabled(button)!=FALSE)!=baselineEnabled) ++failures;
    }
    DestroyWindow(parent);
    std::printf("FANs hidden-page DPI regression: %d failure(s), EC session absent\n", failures);
    return failures ? 4 : 0;
}
