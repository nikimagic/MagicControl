#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <string>
#include <vector>

#include "Geometry.h"
#include "Fonts.h"
#include "Localization.h"
#include "Theme.h"
#include "MagicControl/OperationMessages.h"

namespace mc::gtkui {

namespace ui = mc::gui::ui;
namespace theme = mc::gui::theme;
using Rect = ui::Rect;
using Font = ui::FontRole;
using Color = mc::gui::ColorRef;

enum class Horizontal { Left, Center, Right };
enum class Vertical { Top, Center, Bottom };

double displayScale(GtkWidget* widget = nullptr);
int displayDpi(GtkWidget* widget = nullptr);
void setEmbeddedFontEnabled(bool enabled);
void installUiCss(GtkWidget* reference = nullptr);
void applyWidgetLanguage(GtkWidget* root);
// Preserve the untranslated source in each native property. Language changes
// update these properties, including transient dialogs, without rebuilding UI.
void setWidgetTooltip(GtkWidget*, const char* source);
void setWindowTitle(GtkWindow*, const char* source);
void setColorDialogTitle(GtkColorDialog*, const char* source);
void setWidgetAccessibleLabel(GtkWidget*, const char* source);
void refreshUiLanguage(GtkWindow* root);
// Read-only GDK snapshot; call only from the GTK main-thread dump action.
// Geometry is explicitly logical; derived device sizes are not current modes.
std::string captureDisplayEvidence(GtkWidget* anchor);
void fill(cairo_t* cr, Rect rect, Color color);
void line(cairo_t* cr, int x1, int y1, int x2, int y2, Color color, int width = 1);
void text(cairo_t* cr, const std::string& value, Rect rect, Font font, Color color,
          int dpi, Horizontal horizontal = Horizontal::Left,
          Vertical vertical = Vertical::Center, bool wrap = false);
// Shape the complete string once; individual measured glyph boxes can ellipsize at fractional scales.
void gradientText(cairo_t* cr, const std::string& value, Rect rect, Font font,
                  Color start, Color finish, int dpi);
int textHeight(const std::string& value, Font font, int width, int dpi);
int textWidth(const std::string& value, Font font, int dpi);
void wordmark(cairo_t* cr, Rect rect, int dpi, const std::string& value, Font font);
int showChoiceDialog(GtkWindow* parent,const std::string& message,const std::vector<std::string>& choices);
void auditControl(GtkWidget*);
GtkWidget* actionButton(const char* caption, std::function<void()> callback = {},
                        bool primary = false, bool enabled = true);

// A layout container: it allocates native GTK children to shared Windows
// rectangles. It does not implement input controls or replace GTK scrolling.
class Canvas {
public:
    using Layout = std::function<int(Canvas&, int, int)>;
    using Paint = std::function<void(Canvas&, cairo_t*, int, int)>;

    static Canvas* create(Layout layout, Paint paint, bool scrollContent = true,
                          int fixedLogicalHeight = -1);
    GtkWidget* widget() const { return widget_; }
    void add(GtkWidget* child);
    void place(GtkWidget* child, Rect rect);
    void redraw();
    void setTooltipRegions(std::vector<ui::TooltipRegion> regions);
    int dpi() const { return displayDpi(widget_); }
    double scale() const { return displayScale(widget_); }

    // Called only by the GTK widget class.
    int measureHeight(int logicalWidth);
    void allocate(int logicalWidth, int logicalHeight);
    void snapshot(GtkSnapshot* snapshot);

private:
    struct Child { GtkWidget* widget; Rect rect; };
    GtkWidget* widget_{};
    Layout layout_;
    Paint paint_;
    bool scrollContent_{};
    int fixedLogicalHeight_{-1};
    std::vector<Child> children_;
    std::vector<ui::TooltipRegion> tooltipRegions_;
};

GtkWidget* scrolledWidget(GtkWidget* content);
GtkWidget* scrolledPage(Canvas* canvas);
void showOperationDialog(GtkWindow*,const std::string& title,std::shared_ptr<OperationMessages>,std::function<void()> cancel={});

}  // namespace mc::gtkui
