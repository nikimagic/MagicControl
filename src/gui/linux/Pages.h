#pragma once

#include <gtk/gtk.h>
#include <functional>
#include "MagicControl/Monitoring.h"

namespace mc::gtkui {
class OsdService;
// Optional observation source follows the Windows page's existing injection
// boundary. An empty source retains the platform monitoring reader.
GtkWidget* createFansPage(OsdService* osd=nullptr,std::function<mc::MonitoringSnapshot()> source={});
GtkWidget* createSettingsPage(OsdService* osd=nullptr);
GtkWidget* createKeysPage(OsdService* osd=nullptr);
void suspendKeysPage(GtkWidget* page);
void restoreKeysPage(GtkWidget* page,const char* reason);
GtkWidget* createBiosPage(OsdService* osd=nullptr);
GtkWidget* createOvClockPage();
void refreshOvClockPage(GtkWidget* page);
void refreshBiosProfile(GtkWidget* page);
GtkWidget* createNvidiaGpuPage(OsdService* osd=nullptr);
GtkWidget* createRadeonGpuPage(OsdService* osd=nullptr);
}  // namespace mc::gtkui
