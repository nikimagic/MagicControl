#pragma once
#include <functional>
#include <memory>

namespace mc::gtkui {
// StatusNotifierItem + DBusMenu; the desktop draws its native tray menu.
class TrayIcon {
public:
    TrayIcon(std::function<void()> open, std::function<void()> exit,
             std::function<void(bool)> availabilityChanged);
    ~TrayIcon();
    [[nodiscard]] bool available() const;
    void refreshLanguage();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
