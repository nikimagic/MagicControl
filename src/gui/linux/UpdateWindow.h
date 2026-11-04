#pragma once
#include <gtk/gtk.h>
#include <functional>
namespace mc::gtkui { void showUpdateWindow(GtkWindow* parent,std::function<void()> ready); }
