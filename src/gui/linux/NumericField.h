#pragma once

#include <gtk/gtk.h>

namespace mc::gtkui {

GtkWidget* createNumericField(double minimum, double maximum, double step,bool hexadecimal=false);
void setNumericArrowWidth(GtkWidget* spin, int logicalWidth);
// Shared focus gate for numeric fields and native range controls.
void limitWheelToFocusedField(GtkWidget* widget);
// Page opt-in: clicks outside a numeric field and Enter release its focus.
void installNumericFocusCommit(GtkWidget* scope);

}  // namespace mc::gtkui
