#pragma once
#include <gio/gio.h>
#include <functional>

namespace mc::gtkui {
// Created and destroyed on the owning GLib main context. The connection is
// borrowed by the caller and retained for the lifetime of the subscription.
class SettingsSleepMonitor {
public:
    SettingsSleepMonitor(GDBusConnection* bus,std::function<void(bool)> onTransition);
    ~SettingsSleepMonitor();
    SettingsSleepMonitor(const SettingsSleepMonitor&)=delete;
    SettingsSleepMonitor& operator=(const SettingsSleepMonitor&)=delete;
private:
    GDBusConnection* bus_{};
    guint subscription_{};
    std::function<void(bool)> onTransition_;
};
}
