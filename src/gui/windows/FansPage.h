#pragma once

#include <windows.h>

#include <memory>
#include <functional>
#include <string>
#include "MagicControl/Monitoring.h"

namespace mc {
class EcAccess;
namespace windows { class FanController; }
}

namespace mc::gui {
class OsdPopup;

/// Owns the complete FANs tab: live readings, monitoring cards, graphs and EC curves.
class FansPage {
public:
    FansPage(HWND parent, EcAccess* ec, OsdPopup* osd=nullptr, std::function<void(std::string)> log={},
             std::shared_ptr<mc::windows::FanController> controller={},
             std::function<mc::MonitoringSnapshot()> monitoringSource={});
    ~FansPage();
    FansPage(const FansPage&) = delete;
    FansPage& operator=(const FansPage&) = delete;

    void arrange(const RECT& area, UINT dpi, bool visible);
    [[nodiscard]] bool preTranslate(MSG& message);
    [[nodiscard]] std::wstring statusText() const;
    void refreshCommandAvailability();
    void refreshDeviceState(); // Invalidate old asynchronous observations, then use normal polling.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mc::gui
