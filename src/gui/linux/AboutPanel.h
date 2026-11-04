#pragma once
#include <gtk/gtk.h>
#include <functional>
namespace mc::gtkui {
GtkWidget* createAboutPanel(GtkWindow* parent,int selected,std::function<void(int)> changed,std::function<void(int)> modelChanged={});
void refreshAboutPanel(GtkWidget* panel);
}
