#pragma once

#include <windows.h>
#include <memory>
#include <functional>

namespace mc {
class EcAccess;
struct ApplicationPreferences;
}

namespace mc::gui {
class OsdPopup;

class KeysPage {
public:
    KeysPage(HWND parent, EcAccess* ec, ApplicationPreferences& preferences,
             std::function<void(const ApplicationPreferences&)> applyPreferences, OsdPopup* osd=nullptr);
    ~KeysPage();
    KeysPage(const KeysPage&) = delete;
    KeysPage& operator=(const KeysPage&) = delete;
    void arrange(const RECT& area, UINT dpi, bool visible);
    bool preTranslate(MSG& message);
    void observeState(int row, bool on);
    void refreshCommandAvailability();
    void restore(const std::string& reason);
    void suspend();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mc::gui
