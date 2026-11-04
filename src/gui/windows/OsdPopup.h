#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "../common/OsdModel.h"

namespace mc::gui {
class OsdPopup {
public:
    OsdPopup();
    ~OsdPopup();
    OsdPopup(const OsdPopup&)=delete;
    OsdPopup& operator=(const OsdPopup&)=delete;
    bool create(HINSTANCE instance, std::function<void(std::string)> log={});
    void show(osd::Message message,HWND anchor=nullptr);
    void show(const std::wstring& title, const std::wstring& detail);
    std::uint64_t beginOperation(osd::Message message);
    void completeOperation(std::uint64_t token, osd::Message message);
    bool hasPendingCommand(const std::string& key) const;
    void hide();
    void setEnabled(bool enabled);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
