#pragma once
#include <windows.h>
#include <memory>
#include "MagicControl/GpuDevice.h"
namespace mc::gui {
class OsdPopup;
class DiscreteGpuPage {
public:
    explicit DiscreteGpuPage(HWND parent,GpuVendor vendor=GpuVendor::Nvidia,OsdPopup* osd=nullptr);~DiscreteGpuPage();
    void arrange(const RECT& area,UINT dpi,bool visible);
    void refreshCommandAvailability();
    bool preTranslate(MSG& message);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
