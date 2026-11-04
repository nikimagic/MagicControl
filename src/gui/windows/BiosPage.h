#pragma once
#include <windows.h>
#include <memory>
#include "MagicControl/DiagnosticDump.h"

namespace mc {class EcAccess;}
namespace mc::gui {
class OsdPopup;
// Inventory and explicit diagnostic actions. Native controls own focus;
// collection, bounded fan probes and network work run outside the UI thread.
class BiosPage {
public:
    explicit BiosPage(HWND parent,std::shared_ptr<mc::EcAccess> access={},MaximumFanProbe maximumProbe={},FirmwareFanDefault firmwareDefault={},OsdPopup* popup=nullptr);
    ~BiosPage();
    BiosPage(const BiosPage&) = delete;
    BiosPage& operator=(const BiosPage&) = delete;
    void arrange(const RECT& area, UINT dpi, bool visible);
    bool preTranslate(MSG& message);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
