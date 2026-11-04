#pragma once
#include <gtk/gtk.h>
#include <cstdint>
#include <memory>
#include <optional>
#include "../common/OsdModel.h"

namespace mc::gtkui {
class OsdService {
public:
    OsdService(GtkWindow* owner,GtkOverlay* overlay);
    ~OsdService();
    OsdService(const OsdService&)=delete;
    OsdService& operator=(const OsdService&)=delete;
    void show(mc::gui::osd::Message message);
    void hide(); // Dismiss UI context only; never cancels a hardware command.
    std::uint64_t beginOperation(mc::gui::osd::Message message);
    void completeOperation(std::uint64_t token,mc::gui::osd::Message message);
    void setEnabled(bool enabled);
    void refreshLanguage();
    void stop();
#ifdef MAGICCONTROL_GTK_SETTINGS_TEST
    std::optional<mc::gui::osd::Message> testMessage() const;
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
