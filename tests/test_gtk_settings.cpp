#include <gtk/gtk.h>
#include <cstdio>
#include <cstring>
#include "../src/gui/linux/Pages.h"
#include "../src/gui/linux/OsdService.h"
#include "../src/gui/common/DeviceHeader.h"
#include "MagicControl/BoardIdentity.h"

namespace mc::gtkui { bool testSettingsThreadStartFailure(GtkWidget*); }

namespace {
int customLabels{};
void inspect(GtkWidget* widget) {
    if (GTK_IS_LABEL(widget) && std::strcmp(gtk_label_get_text(GTK_LABEL(widget)), "Свой цвет…") == 0)
        ++customLabels;
    for (auto* child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) inspect(child);
}
}
int main() {
    if (!gtk_init_check()) { std::puts("GTK display unavailable: skipped"); return 77; }
    // Make the old dangling-child assertion fail the regression immediately.
    g_log_set_always_fatal(static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL));
    const int actual=mc::gui::ui::currentLaptop(mc::readBoardDescriptor());
    mc::gui::ui::laptopSelection.store(actual==0?1:0); // Preview never opens an OEM backend.
    auto* window=GTK_WINDOW(gtk_window_new());g_object_ref_sink(window);
    auto* overlay=GTK_OVERLAY(gtk_overlay_new());gtk_window_set_child(window,GTK_WIDGET(overlay));
    bool failureRestored{};
    {
        mc::gtkui::OsdService osd(window,overlay);osd.setEnabled(true);
        auto* page=mc::gtkui::createSettingsPage(&osd);g_object_ref_sink(page);
        inspect(page);failureRestored=mc::gtkui::testSettingsThreadStartFailure(page);
        g_object_unref(page);
    }
    const bool valid = customLabels == 1 && failureRestored;
    gtk_window_destroy(window);g_object_unref(window);
    mc::gui::ui::laptopSelection.store(-1);
    std::printf("GTK SETs custom color label: %d, startup, worker and sleep recovery: %d, EC writes=0\n", customLabels, failureRestored);
    return valid ? 0 : 1;
}
