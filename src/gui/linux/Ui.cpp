#include "MagicControl/ActionLog.h"
#include <atomic>
#include "Ui.h"
#include "MagicControl/Logging.h"
#include "../../platform/common/DisplayEvidence.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <memory>
#include <set>
#include <cstdint>
#include <dlfcn.h>

namespace {
struct McCanvas { GtkWidget parent; mc::gtkui::Canvas* owner; };
struct McCanvasClass { GtkWidgetClass parent; };
G_DEFINE_TYPE(McCanvas, mc_canvas, GTK_TYPE_WIDGET)

GtkSizeRequestMode requestMode(GtkWidget*) { return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH; }
void measure(GtkWidget* widget, GtkOrientation orientation, int forSize,
              int* minimum, int* natural, int* minimumBaseline, int* naturalBaseline) {
    auto* self = reinterpret_cast<McCanvas*>(widget);
    const int height = orientation == GTK_ORIENTATION_VERTICAL && self->owner
        ? self->owner->measureHeight(forSize > 0 ? forSize : mc::gui::kDefaultWidth) : 0;
    *minimum = *natural = height;
    *minimumBaseline = *naturalBaseline = -1;
}
void allocate(GtkWidget* widget, int width, int height, int) {
    auto* self = reinterpret_cast<McCanvas*>(widget);
    if (self->owner) self->owner->allocate(width, height);
}
void snapshot(GtkWidget* widget, GtkSnapshot* snapshotValue) {
    auto* self = reinterpret_cast<McCanvas*>(widget);
    if (self->owner) self->owner->snapshot(snapshotValue);
}
void dispose(GObject* object) {
    auto* widget = GTK_WIDGET(object);
    while (GtkWidget* child = gtk_widget_get_first_child(widget)) gtk_widget_unparent(child);
    G_OBJECT_CLASS(mc_canvas_parent_class)->dispose(object);
}
void mc_canvas_class_init(McCanvasClass* klass) {
    auto* widget = GTK_WIDGET_CLASS(klass);
    widget->get_request_mode = requestMode;
    widget->measure = measure;
    widget->size_allocate = allocate;
    widget->snapshot = snapshot;
    gtk_widget_class_set_css_name(widget, "magiccontrol-canvas");
    gtk_widget_class_set_accessible_role(widget, GTK_ACCESSIBLE_ROLE_GROUP);
    G_OBJECT_CLASS(klass)->dispose = dispose;
}
void mc_canvas_init(McCanvas*) {}

std::string uiFamily() {
    return mc::gui::ui::selectedFontFamily(
        static_cast<mc::gui::ui::Language>(mc::gui::ui::languageIndex()));
}

PangoLayout* createText(cairo_t* cr, const std::string& value, mc::gtkui::Font role,
                         int width, int dpi, bool wrap) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_context_set_language(pango_layout_get_context(layout), pango_language_from_string(
        mc::gui::ui::languageTag(static_cast<mc::gui::ui::Language>(mc::gui::ui::languageIndex()))));
    PangoFontDescription* description = pango_font_description_new();
    const std::string family = uiFamily();
    pango_font_description_set_family(description, family.c_str());
    pango_font_description_set_absolute_size(description,
        mc::gui::ui::fontHeight(role, dpi) * PANGO_SCALE);
    pango_font_description_set_weight(description,
        static_cast<PangoWeight>(mc::gui::ui::font(role).weight));
    pango_layout_set_font_description(layout, description);
    pango_font_description_free(description);
    const auto translated=mc::gui::ui::translate(value);
    pango_layout_set_text(layout, translated.c_str(), static_cast<int>(translated.size()));
    pango_layout_set_width(layout, std::max(1, width) * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_single_paragraph_mode(layout, !wrap);
    if (!wrap) {
        pango_layout_set_height(layout, -1);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    }
    return layout;
}

std::string cssColor(mc::gtkui::Color color) {
    std::ostringstream out;
    out << '#' << std::hex << std::setfill('0')
        << std::setw(2) << (color & 255)
        << std::setw(2) << ((color >> 8) & 255)
        << std::setw(2) << ((color >> 16) & 255);
    return out.str();
}
}  // namespace

namespace mc::gtkui {
namespace {
std::string cssFontFamily(const std::string& family){
    std::string result;std::size_t start{};
    while(start<family.size()){
        const auto end=family.find(',',start);auto name=family.substr(start,end==std::string::npos?std::string::npos:end-start);
        const auto first=name.find_first_not_of(" \t"),last=name.find_last_not_of(" \t");
        if(first!=std::string::npos){if(!result.empty())result+=", ";result+='\'';
            for(const auto ch:name.substr(first,last-first+1)){if(ch=='\\'||ch=='\'')result+='\\';result+=ch;}result+='\'';}
        if(end==std::string::npos)break;start=end+1;
    }
    return result;
}
struct WindowTypography {
    GtkCssProvider* provider{};GdkDisplay* display{};std::string name,family;
    double scale{};int dpi{};unsigned language{~0u};bool dormant{};
    void release(){
        // A provider is display-registered, but its lifetime belongs to this
        // root window. Remove both references at unrealize/destroy, not merely
        // at application shutdown. Remapping recreates one fresh registration.
        if(provider){gtk_style_context_remove_provider_for_display(display,GTK_STYLE_PROVIDER(provider));g_object_unref(provider);provider=nullptr;}
        if(display){g_object_unref(display);display=nullptr;}family.clear();language=~0u;
    }
    ~WindowTypography(){release();}
};
void updateWindowTypography(GtkWidget* window,const std::string& family){
    if(!GTK_IS_WINDOW(window))return;
    auto* value=static_cast<WindowTypography*>(g_object_get_data(G_OBJECT(window),"mc-window-typography"));
    if(!value){
        static std::uint64_t next{};value=new WindowTypography;value->name="mc-fonts-"+std::to_string(++next);
        gtk_widget_add_css_class(window,value->name.c_str());
        g_object_set_data_full(G_OBJECT(window),"mc-window-typography",value,[](gpointer p){delete static_cast<WindowTypography*>(p);});
        g_signal_connect(window,"unrealize",G_CALLBACK(+[](GtkWidget*,gpointer p){auto* value=static_cast<WindowTypography*>(p);value->dormant=true;value->release();}),value);
    }
    // A destroyed root can still have external references. A refresh of some
    // other window must not re-register its released provider. Only its own
    // subsequent map signal may reactivate the same scope.
    if(value->dormant)return;
    auto* display=gtk_widget_get_display(window);const double scale=displayScale(window);const int dpi=displayDpi(window);
    if(value->display!=display)value->release();
    if(!value->provider){value->display=GDK_DISPLAY(g_object_ref(display));value->provider=gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(display,GTK_STYLE_PROVIDER(value->provider),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+1);}
    if(value->family==family&&value->scale==scale&&value->dpi==dpi&&value->language==ui::languageIndex())return;
    value->family=family;value->scale=scale;value->dpi=dpi;value->language=ui::languageIndex();
    const auto px=[=](Font role){return ui::fontHeight(role,dpi)/scale;};
    const std::string prefix="window."+value->name;const auto families=cssFontFamily(family);
    std::ostringstream css;
    // Typography ONLY. The base provider still owns every colour, border,
    // control/scrollbar dimension, font weight and reference baseline.
    css<<prefix<<", "<<prefix<<" * {font-family:"<<families<<";font-size:"<<px(Font::Body)<<"px;}\n"
       <<prefix<<" label.language-option-label {font-family:"<<families<<";font-size:"<<px(Font::Body)<<"px;}\n"
       <<prefix<<" button.curve-preset, "<<prefix<<" .numeric-field, "<<prefix<<" .numeric-field text, "
       <<prefix<<" .numeric-field button {font-size:"<<px(Font::Caption)<<"px;}\n"
       <<prefix<<" label.selectable-data {font-size:"<<px(Font::ClassicBody)<<"px;}\n"
       <<prefix<<" label.about-heading {font-size:"<<px(Font::ClassicHeading)<<"px;}\n"
       <<prefix<<" .heading-action label {font-size:"<<px(Font::ClassicHeading)<<"px;font-weight:700;}\n"
       <<prefix<<" label.about-secondary, "<<prefix<<" label.about-detail {font-size:"<<px(Font::KeyDetail)<<"px;}\n";
    gtk_css_provider_load_from_string(value->provider,css.str().c_str());
}
void watchNewWindows(){
    static bool installed{};if(installed)return;installed=true;
    // Native GTK dialogs may create another GtkWindow after their title object
    // was configured. Discover them after construction, without retaining roots.
    g_signal_connect(gtk_window_get_toplevels(),"items-changed",G_CALLBACK(+[](GListModel*,guint,guint,guint,gpointer){
        static guint pending{};if(pending)return;
        pending=g_idle_add(+[](gpointer)->gboolean{
            pending=0;auto* windows=gtk_window_get_toplevels();
            for(guint i=0;i<g_list_model_get_n_items(windows);++i){auto* root=GTK_WIDGET(g_list_model_get_item(windows,i));if(!root)continue;applyWidgetLanguage(root);g_object_unref(root);}
            installUiCss();return G_SOURCE_REMOVE;
        },nullptr);
    }),nullptr);
}
// Weak membership only: neither an open color dialog nor a transient window is
// kept alive by localization. All calls and notify handlers run on the GTK thread.
std::set<GObject*>& languageObjects(){static auto* objects=new std::set<GObject*>;return *objects;}
void rememberLanguageObject(GObject* object){
    if(languageObjects().insert(object).second)
        g_object_weak_ref(object,+[](gpointer,GObject* gone){languageObjects().erase(gone);},nullptr);
}
struct LocalizedProperty {std::string name,original,last;bool initialized{},updating{};};
struct LocalizedProperties {std::vector<std::unique_ptr<LocalizedProperty>> values;};
void translateProperty(GObject* object,LocalizedProperty& value){
    if(value.updating)return;
    gchar* raw{};g_object_get(object,value.name.c_str(),&raw,nullptr);
    const std::string current=raw?raw:"";g_free(raw);
    if(!value.initialized||current!=value.last)value.original=current;
    value.initialized=true;value.last=ui::translate(value.original);
    if(current!=value.last){value.updating=true;g_object_set(object,value.name.c_str(),value.last.c_str(),nullptr);value.updating=false;}
}
void bindProperty(GObject* object,const char* name){
    auto* values=static_cast<LocalizedProperties*>(g_object_get_data(object,"mc-language-properties"));
    if(!values){values=new LocalizedProperties;g_object_set_data_full(object,"mc-language-properties",values,[](gpointer p){delete static_cast<LocalizedProperties*>(p);});rememberLanguageObject(object);}
    for(auto& value:values->values)if(value->name==name){translateProperty(object,*value);return;}
    auto value=std::make_unique<LocalizedProperty>();value->name=name;auto* stable=value.get();values->values.push_back(std::move(value));
    const std::string signal="notify::"+std::string(name);
    g_signal_connect(object,signal.c_str(),G_CALLBACK(+[](GObject* o,GParamSpec*,gpointer p){translateProperty(o,*static_cast<LocalizedProperty*>(p));}),stable);
    translateProperty(object,*stable);
}
void applyAccessibleLabel(GtkWidget* widget){
    if(const auto* source=static_cast<std::string*>(g_object_get_data(G_OBJECT(widget),"mc-accessible-source"))){
        const auto value=ui::translate(*source);
        gtk_accessible_update_property(GTK_ACCESSIBLE(widget),GTK_ACCESSIBLE_PROPERTY_LABEL,value.c_str(),-1);
    }
}
void typographyChanged(GtkWidget* widget){
    installUiCss(widget);gtk_widget_queue_resize(widget);gtk_widget_queue_draw(widget);
}
void watchTypography(GtkWidget* window){
    if(g_object_get_data(G_OBJECT(window),"mc-typography-watch"))return;
    g_object_set_data(G_OBJECT(window),"mc-typography-watch",GINT_TO_POINTER(1));
    g_signal_connect(window,"notify::scale-factor",G_CALLBACK(+[](GObject* object,GParamSpec*,gpointer){typographyChanged(GTK_WIDGET(object));}),nullptr);
    g_signal_connect(window,"map",G_CALLBACK(+[](GtkWidget* widget,gpointer){
        if(auto* value=static_cast<WindowTypography*>(g_object_get_data(G_OBJECT(widget),"mc-window-typography")))value->dormant=false;
        typographyChanged(widget);
        if(auto* surface=gtk_native_get_surface(GTK_NATIVE(widget));surface&&!g_object_get_data(G_OBJECT(surface),"mc-typography-watch")){
            g_object_set_data(G_OBJECT(surface),"mc-typography-watch",GINT_TO_POINTER(1));
            // Fractional scale can change without the integer widget factor.
            if(g_object_class_find_property(G_OBJECT_GET_CLASS(surface),"scale"))
                g_signal_connect_object(surface,"notify::scale",G_CALLBACK(+[](GObject*,GParamSpec*,gpointer target){typographyChanged(GTK_WIDGET(target));}),widget,G_CONNECT_DEFAULT);
        }
    }),nullptr);
    installUiCss(window);
}
void setSourceProperty(GObject* object,const char* name,const char* source){
    // Feed the source to the native property so its notify handler also sees
    // subsequent dynamic status changes. The adapter retains the original.
    g_object_set(object,name,source?source:"",nullptr);bindProperty(object,name);
}
}
void applyWidgetLanguage(GtkWidget* root){
    pango_context_set_language(gtk_widget_get_pango_context(root),pango_language_from_string(
        ui::languageTag(static_cast<ui::Language>(ui::languageIndex()))));
    if(GTK_IS_LABEL(root))bindProperty(G_OBJECT(root),"label");
    if(GTK_IS_WINDOW(root)){bindProperty(G_OBJECT(root),"title");watchTypography(root);}
    bindProperty(G_OBJECT(root),"tooltip-text");
    applyAccessibleLabel(root);
    for(auto* child=gtk_widget_get_first_child(root);child;child=gtk_widget_get_next_sibling(child))applyWidgetLanguage(child);
}
void setWidgetTooltip(GtkWidget* widget,const char* source){setSourceProperty(G_OBJECT(widget),"tooltip-text",source);}
void setWindowTitle(GtkWindow* window,const char* source){setSourceProperty(G_OBJECT(window),"title",source);}
void setColorDialogTitle(GtkColorDialog* dialog,const char* source){setSourceProperty(G_OBJECT(dialog),"title",source);}
void setWidgetAccessibleLabel(GtkWidget* widget,const char* source){
    g_object_set_data_full(G_OBJECT(widget),"mc-accessible-source",new std::string(source?source:""),[](gpointer p){delete static_cast<std::string*>(p);});
    rememberLanguageObject(G_OBJECT(widget));applyAccessibleLabel(widget);
}
void refreshUiLanguage(GtkWindow* root){
    // Discover freshly created labels in all transient/application windows.
    auto* windows=gtk_window_get_toplevels();
    for(guint i=0;i<g_list_model_get_n_items(windows);++i){auto* window=GTK_WIDGET(g_list_model_get_item(windows,i));if(!window)continue;applyWidgetLanguage(window);gtk_widget_queue_resize(window);gtk_widget_queue_draw(window);g_object_unref(window);}
    std::vector<GObject*> snapshot;for(auto* object:languageObjects())snapshot.push_back(G_OBJECT(g_object_ref(object)));
    for(auto* object:snapshot){
        if(auto* properties=static_cast<LocalizedProperties*>(g_object_get_data(object,"mc-language-properties")))
            for(auto& property:properties->values)translateProperty(object,*property);
        if(GTK_IS_WIDGET(object))applyAccessibleLabel(GTK_WIDGET(object));g_object_unref(object);
    }
    installUiCss(GTK_WIDGET(root));
}
std::string captureDisplayEvidence(GtkWidget* anchor){
    using namespace mc::diagnostics;
    DisplayEvidence out;out.source="GDK monitor snapshot captured on GTK main thread before dump worker";
    if(!g_main_context_is_owner(g_main_context_default())){
        out.status="Unavailable: capture requires the GTK main context owner";
        out.issues.push_back({"enumeration","GDK","Off-main-thread GDK access refused",{}, {}});return displayEvidenceJson(out);
    }
    auto* display=anchor?gtk_widget_get_display(anchor):gdk_display_get_default();
    if(!display){out.status="No GDK display";out.issues.push_back({"enumeration","GDK","No display connection",{}, {}});return displayEvidenceJson(out);}
    auto* root=anchor?gtk_widget_get_root(anchor):nullptr;
    auto* surface=root&&GTK_IS_NATIVE(root)?gtk_native_get_surface(GTK_NATIVE(root)):nullptr;
    auto* windowMonitor=surface?gdk_display_get_monitor_at_surface(display,surface):nullptr;
    const double surfaceScale=surface?gdk_surface_get_scale(surface):0;
    // The build supports GTK4.12. Do not introduce a mandatory >=4.14 runtime
    // symbol merely because newer build headers expose Monitor.get_scale().
    using MonitorScaleFn=double(*)(GdkMonitor*);
    MonitorScaleFn monitorScale{};
    // Runtime gating also permits a4.12-header build to use a newer installed
    // GTK without referencing the >=4.14 symbol in its mandatory imports.
    if(gtk_check_version(4,14,0)==nullptr)
        monitorScale=reinterpret_cast<MonitorScaleFn>(dlsym(RTLD_DEFAULT,"gdk_monitor_get_scale"));
    auto* monitors=gdk_display_get_monitors(display);const auto count=g_list_model_get_n_items(monitors);
    out.enumerationComplete=count<=128;out.status="Current GDK desktop evidence; current physical mode/primary unavailable through GDK";
    if(count>128)out.issues.push_back({"enumeration","gdk_display_get_monitors","Monitor safety limit exceeded",{}, {}});
    for(guint index=0;index<std::min(count,guint(128));++index){
        auto* monitor=GDK_MONITOR(g_list_model_get_item(monitors,index));
        if(!monitor){out.enumerationComplete=false;out.issues.push_back({"enumeration","g_list_model_get_item","Monitor removed during non-atomic snapshot at index "+std::to_string(index),{}, {}});continue;}
        DisplayReading row;row.source=out.source;row.id=gdk_monitor_get_connector(monitor)?gdk_monitor_get_connector(monitor):"gdk-monitor-"+std::to_string(index);
        const char* model=gdk_monitor_get_model(monitor),*maker=gdk_monitor_get_manufacturer(monitor);
        row.name=model?model:row.id;const bool valid=gdk_monitor_is_valid(monitor)!=FALSE;row.connected=valid;
        row.status="GDK application-coordinate evidence; derived device dimensions are not a direct compositor mode";
        GdkRectangle geometry{};gdk_monitor_get_geometry(monitor,&geometry);
        const int factor=gdk_monitor_get_scale_factor(monitor),rate=gdk_monitor_get_refresh_rate(monitor);
        if(rate>0){row.refreshNumerator=rate;row.refreshDenominator=1000;row.refreshHz=rate/1000.0;row.refreshSource="gdk_monitor_get_refresh_rate; millihertz/1000, reported current monitor frequency, not frame rate";}
        else row.issues.push_back({"refreshHz","gdk_monitor_get_refresh_rate","GDK reports unknown refresh (zero)",{}, {}});
        const double scale=monitorScale?monitorScale(monitor):0;
        if(std::isfinite(scale)&&scale>0){row.scalePercent=scale*100.0;row.scaleSource="gdk_monitor_get_scale (>=4.14 optional symbol); application-coordinate to device-pixel factor";
            row.dpiX=row.dpiY=scale*96;row.dpiSource="Derived 96 * GDK monitor scale; application effective DPI convention, not physical panel DPI or independent OS custom override";}
        else row.issues.push_back({"scalePercent/effectiveDpi","gdk_monitor_get_scale","Fractional monitor scale unavailable; rounded integer scale_factor not substituted",{}, {}});
        row.issues.push_back({"pixelWidth/pixelHeight","GDK","Geometry is application/logical pixels; direct compositor current physical mode not exposed. Derived dimensions are metadata only",{}, {}});
        row.issues.push_back({"primary","GDK4","Primary monitor is not exposed; first monitor and application window monitor are not assumed primary",{}, {}});
        row.issues.push_back({"active","GDK4","A valid monitor object establishes availability, not an independently queried compositor active path",{}, {}});
        row.metadataJson="{\"manufacturer\":"+firmwareQuote(maker?maker:"")+",\"logicalGeometry\":{\"x\":"+std::to_string(geometry.x)+",\"y\":"+std::to_string(geometry.y)+",\"width\":"+std::to_string(geometry.width)+",\"height\":"+std::to_string(geometry.height)+",\"units\":\"GDK application pixels\",\"source\":\"gdk_monitor_get_geometry\"},\"integerScaleFactor\":"+std::to_string(factor)+
            ",\"monitorScale\":"+displayNumber(std::isfinite(scale)&&scale>0?std::optional<double>(scale):std::nullopt)+
            ",\"derivedDeviceDimensions\":{\"derived\":true,\"width\":"+displayNumber(scale>0?std::optional<double>(geometry.width*scale):std::nullopt)+",\"height\":"+displayNumber(scale>0?std::optional<double>(geometry.height*scale):std::nullopt)+",\"source\":\"GDK geometry * monitor scale; not queried physical current mode\"},\"applicationWindowOnMonitor\":"+(monitor==windowMonitor?"true":"false")+"}";
        if(!valid){out.enumerationComplete=false;row.issues.push_back({"connected","gdk_monitor_is_valid","Monitor invalidated during non-atomic snapshot",{}, {}});}
        out.displays.push_back(std::move(row));g_object_unref(monitor);
    }
    out.metadataJson="{\"guiSnapshotProvided\":true,\"gdkBackendType\":"+firmwareQuote(G_OBJECT_TYPE_NAME(display))+",\"gtkRuntime\":"+firmwareQuote(std::to_string(gtk_get_major_version())+"."+std::to_string(gtk_get_minor_version())+"."+std::to_string(gtk_get_micro_version()))+
        ",\"applicationSurfaceScale\":"+displayNumber(surface&&std::isfinite(surfaceScale)&&surfaceScale>0?std::optional<double>(surfaceScale):std::nullopt)+",\"applicationSurfaceScaleSource\":\"gdk_surface_get_scale for the dump action window only; not all monitor user scales\",\"edidPreferredUsedAsCurrent\":false}";
    return displayEvidenceJson(out);
}
void setEmbeddedFontEnabled(bool) {} // Legacy API; bundled fonts are mandatory.

double displayScale(GtkWidget* widget) {
    if (widget) {
        GtkRoot* root = gtk_widget_get_root(widget);
        if (root && GTK_IS_NATIVE(root)) {
            GdkSurface* surface = gtk_native_get_surface(GTK_NATIVE(root));
            if (surface) return gdk_surface_get_scale(surface);
        }
    }
    GdkDisplay* display = gdk_display_get_default();
    GListModel* monitors = display ? gdk_display_get_monitors(display) : nullptr;
    if (!monitors || !g_list_model_get_n_items(monitors)) return 1.0;
    auto* monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
    if(!monitor)return 1.0;
    const int scale = gdk_monitor_get_scale_factor(monitor);
    g_object_unref(monitor);
    return std::max(1, scale);
}
int displayDpi(GtkWidget* widget) { return static_cast<int>(std::lround(displayScale(widget) * 96.0)); }

void fill(cairo_t* cr, Rect rect, Color color) {
    cairo_set_source_rgb(cr, (color & 255) / 255.0, ((color >> 8) & 255) / 255.0,
                          ((color >> 16) & 255) / 255.0);
    cairo_rectangle(cr, rect.left, rect.top, rect.width(), rect.height());
    cairo_fill(cr);
}
void line(cairo_t* cr, int x1, int y1, int x2, int y2, Color color, int width) {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_source_rgb(cr, (color & 255) / 255.0, ((color >> 8) & 255) / 255.0,
                          ((color >> 16) & 255) / 255.0);
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, x1 + (width % 2 ? 0.5 : 0.0), y1 + (width % 2 ? 0.5 : 0.0));
    cairo_line_to(cr, x2 + (width % 2 ? 0.5 : 0.0), y2 + (width % 2 ? 0.5 : 0.0));
    cairo_stroke(cr);
    cairo_restore(cr);
}
void text(cairo_t* cr, const std::string& value, Rect rect, Font role, Color color,
           int dpi, Horizontal horizontal, Vertical vertical, bool wrap) {
    if (rect.width() <= 0 || rect.height() <= 0 || value.empty()) return;
    PangoLayout* layout = createText(cr, value, role, rect.width(), dpi, wrap);
    pango_layout_set_alignment(layout, horizontal == Horizontal::Left ? PANGO_ALIGN_LEFT :
        horizontal == Horizontal::Right ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_CENTER);
    const auto reference = ui::textMetrics(role, dpi);
    const int lines = wrap ? pango_layout_get_line_count(layout) : 1;
    const int height = reference.height * lines;
    const int top = vertical == Vertical::Top ? rect.top : vertical == Vertical::Bottom
        ? rect.bottom - height : rect.top + (rect.height() - height) / 2;
    cairo_save(cr);
    cairo_rectangle(cr, rect.left, rect.top, rect.width(), rect.height());
    cairo_clip(cr);
    cairo_set_source_rgb(cr, (color & 255) / 255.0, ((color >> 8) & 255) / 255.0,
                          ((color >> 16) & 255) / 255.0);
    // Draw every baseline explicitly. Pango's line-spacing factor follows the
    // Cairo transform and can halve the distance on a scaled GSK surface.
    // Shared line boxes must also be the actual distance between wrapped lines.
    int row = 0;
    for (GSList* entry = pango_layout_get_lines_readonly(layout); entry; entry = entry->next, ++row) {
        auto* line = static_cast<PangoLayoutLine*>(entry->data);
        PangoRectangle logical{}; pango_layout_line_get_pixel_extents(line, nullptr, &logical);
        const int left = horizontal == Horizontal::Right ? rect.right - logical.width
            : horizontal == Horizontal::Center ? rect.left + (rect.width() - logical.width) / 2 : rect.left;
        cairo_move_to(cr, left, top + row * reference.height + reference.ascent);
        pango_cairo_show_layout_line(cr, line);
    }
    cairo_restore(cr);
    g_object_unref(layout);
}
void gradientText(cairo_t* cr,const std::string& value,Rect rect,Font role,Color start,Color finish,int dpi) {
    if(value.empty()||rect.width()<=0||rect.height()<=0)return;
    auto* layout=createText(cr,value,role,100000,dpi,false);
    pango_layout_set_width(layout,-1);pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_NONE);
    int width{};pango_layout_get_pixel_size(layout,&width,nullptr);
    const auto metrics=ui::textMetrics(role,dpi);
    const double left=rect.right-width;
    const double top=rect.top+(rect.height()-metrics.height)/2.0+metrics.ascent-
        pango_layout_get_baseline(layout)/static_cast<double>(PANGO_SCALE);
    cairo_save(cr);cairo_rectangle(cr,rect.left,rect.top,rect.width(),rect.height());cairo_clip(cr);
    cairo_move_to(cr,left,top);pango_cairo_layout_path(cr,layout);
    auto* gradient=cairo_pattern_create_linear(left,0,left+std::max(1,width),0);
    cairo_pattern_add_color_stop_rgb(gradient,0,(start&255)/255.0,((start>>8)&255)/255.0,((start>>16)&255)/255.0);
    cairo_pattern_add_color_stop_rgb(gradient,1,(finish&255)/255.0,((finish>>8)&255)/255.0,((finish>>16)&255)/255.0);
    cairo_set_source(cr,gradient);cairo_fill(cr);cairo_pattern_destroy(gradient);
    cairo_restore(cr);g_object_unref(layout);
}
int textHeight(const std::string& value, Font role, int width, int dpi) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surface);
    PangoLayout* layout = createText(cr, value.empty() ? " " : value, role, width, dpi, true);
    const int result = pango_layout_get_line_count(layout) * ui::textMetrics(role, dpi).height;
    g_object_unref(layout); cairo_destroy(cr); cairo_surface_destroy(surface);
    return result;
}
int textWidth(const std::string& value, Font role, int dpi) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surface);
    PangoLayout* layout = createText(cr, value, role, 100000, dpi, false);
    pango_layout_set_width(layout, -1);
    int width{}; pango_layout_get_pixel_size(layout, &width, nullptr);
    g_object_unref(layout); cairo_destroy(cr); cairo_surface_destroy(surface);
    return width;
}

void wordmark(cairo_t* cr, Rect rect, int dpi, const std::string& value, Font font) {
    if(value!="MECHREVO"||font!=Font::Brand||rect.width()<ui::brandTextWidth(dpi)) {
        text(cr,value,rect,font,theme::kTextPrimary,dpi,Horizontal::Left,Vertical::Top);return;
    }
    PangoLayout* layout = createText(cr, value, Font::Brand, rect.width(), dpi, false);
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    int width{};
    pango_layout_get_pixel_size(layout, &width, nullptr);
    const auto reference = ui::textMetrics(Font::Brand, dpi);
    const double baseline = pango_layout_get_baseline(layout) / static_cast<double>(PANGO_SCALE);
    const double top = rect.top + (rect.height() - reference.height) / 2;
    cairo_save(cr);
    cairo_set_source_rgb(cr, (theme::kTextPrimary & 255) / 255.0,
        ((theme::kTextPrimary >> 8) & 255) / 255.0, ((theme::kTextPrimary >> 16) & 255) / 255.0);
    cairo_translate(cr, rect.left, top + reference.ascent - baseline);
    cairo_scale(cr, rect.width() / static_cast<double>(std::max(1, width)), 1);
    cairo_move_to(cr, 0, 0);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
    g_object_unref(layout);
}

Canvas* Canvas::create(Layout layout, Paint paint, bool scrollContent, int fixedLogicalHeight) {
    auto* result = new Canvas();
    result->layout_ = std::move(layout); result->paint_ = std::move(paint);
    result->scrollContent_ = scrollContent; result->fixedLogicalHeight_ = fixedLogicalHeight;
    result->widget_ = GTK_WIDGET(g_object_new(mc_canvas_get_type(), nullptr));
    reinterpret_cast<McCanvas*>(result->widget_)->owner = result;
    g_object_set_data_full(G_OBJECT(result->widget_), "magiccontrol-canvas", result,
        [](gpointer data) { delete static_cast<Canvas*>(data); });
    gtk_widget_set_hexpand(result->widget_, TRUE);
    g_signal_connect(result->widget_,"query-tooltip",G_CALLBACK((+[](GtkWidget*,int x,int y,gboolean keyboard,GtkTooltip* tip,gpointer data)->gboolean{
        if(keyboard)return FALSE;
        auto& canvas=*static_cast<Canvas*>(data);const double factor=canvas.scale();
        const int px=static_cast<int>(std::lround(x*factor)),py=static_cast<int>(std::lround(y*factor));
        for(const auto& region:canvas.tooltipRegions_){const auto& bounds=region.bounds;
            if(px<bounds.left||px>=bounds.right||py<bounds.top||py>=bounds.bottom)continue;
            gtk_tooltip_set_text(tip,region.text.c_str());
            GdkRectangle area{static_cast<int>(bounds.left/factor),static_cast<int>(bounds.top/factor),
                static_cast<int>(std::ceil(bounds.width()/factor)),static_cast<int>(std::ceil(bounds.height()/factor))};
            gtk_tooltip_set_tip_area(tip,&area);return TRUE;
        }
        return FALSE;
    })),result);
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, int, double x, double y, gpointer) {
        GtkWidget* canvas = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
        GtkWidget* picked = gtk_widget_pick(canvas, x, y, GTK_PICK_DEFAULT);
        for (GtkWidget* child = picked; child && child != canvas; child = gtk_widget_get_parent(child))
            if (GTK_IS_SPIN_BUTTON(child)) return;
        if (GtkRoot* root = gtk_widget_get_root(canvas)) gtk_root_set_focus(root, nullptr);
    }), nullptr);
    gtk_widget_add_controller(result->widget_, GTK_EVENT_CONTROLLER(click));
    return result;
}
void Canvas::add(GtkWidget* child) {
    if(GTK_IS_BUTTON(child)||GTK_IS_SPIN_BUTTON(child)||GTK_IS_CHECK_BUTTON(child))auditControl(child);
    children_.push_back({child, {}});
    gtk_widget_set_parent(child, widget_);
}
void Canvas::place(GtkWidget* child, Rect rect) {
    for (auto& entry : children_) if (entry.widget == child) { entry.rect = rect; return; }
}
void Canvas::redraw() { gtk_widget_queue_draw(widget_); }
void Canvas::setTooltipRegions(std::vector<ui::TooltipRegion> regions){
    const bool enabled=!regions.empty();tooltipRegions_=std::move(regions);
    if(gtk_widget_get_has_tooltip(widget_)!=enabled)gtk_widget_set_has_tooltip(widget_,enabled);
}
int Canvas::measureHeight(int logicalWidth) {
    if (fixedLogicalHeight_ >= 0) return fixedLogicalHeight_;
    if (!scrollContent_) return 0;
    const int extent = layout_(*this, static_cast<int>(std::lround(logicalWidth * scale())), 0);
    return static_cast<int>(std::ceil(extent / scale()));
}
void Canvas::allocate(int logicalWidth, int logicalHeight) {
    applyWidgetLanguage(widget_);
    const double factor = scale();
    layout_(*this, static_cast<int>(std::lround(logicalWidth * factor)),
                   static_cast<int>(std::lround(logicalHeight * factor)));
    for (auto& child : children_) {
        if (!gtk_widget_get_visible(child.widget) || child.rect.width() <= 0 || child.rect.height() <= 0) continue;
        const graphene_point_t point{static_cast<float>(child.rect.left / factor),
                                      static_cast<float>(child.rect.top / factor)};
        GskTransform* transform = gsk_transform_translate(nullptr, &point);
        gtk_widget_allocate(child.widget,
            std::max(1, static_cast<int>(std::lround(child.rect.width() / factor))),
            std::max(1, static_cast<int>(std::lround(child.rect.height() / factor))), -1, transform);
    }
}
void Canvas::snapshot(GtkSnapshot* snapshotValue) {
    const float width = static_cast<float>(gtk_widget_get_width(widget_));
    const float height = static_cast<float>(gtk_widget_get_height(widget_));
    const graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
    cairo_t* cr = gtk_snapshot_append_cairo(snapshotValue, &bounds);
    cairo_scale(cr, 1.0 / scale(), 1.0 / scale());
    paint_(*this, cr, static_cast<int>(std::lround(width * scale())),
                      static_cast<int>(std::lround(height * scale())));
    cairo_destroy(cr);
    for (auto& child : children_) if (gtk_widget_get_visible(child.widget))
        gtk_widget_snapshot_child(widget_, child.widget, snapshotValue);
}

namespace {
std::uint64_t auditWidgetId(GtkWidget* w){
    static std::atomic<std::uint64_t> next{1};auto id=reinterpret_cast<std::uintptr_t>(g_object_get_data(G_OBJECT(w),"mc-audit-id"));
    if(!id){id=next.fetch_add(1);g_object_set_data(G_OBJECT(w),"mc-audit-id",reinterpret_cast<gpointer>(id));}return id;
}
std::string auditGtkState(GtkWidget* parent){
    std::string out="[";unsigned count{};
    const auto visit=[&](auto&& self,GtkWidget* w)->void{
        if(!w||count>=256)return;
        if(g_object_get_data(G_OBJECT(w),"mc-audit-id")){
            if(count++)out+=',';out+="{\"control_id\":"+std::to_string(auditWidgetId(w))+",\"class\":"+mc::audit::quote(G_OBJECT_TYPE_NAME(w))+",\"enabled\":"+(gtk_widget_get_sensitive(w)?"true":"false");
            if(GTK_IS_BUTTON(w)){const char* c=gtk_button_get_label(GTK_BUTTON(w));out+=",\"caption\":"+mc::audit::quote(c?c:"")+",\"selected\":"+(gtk_widget_has_css_class(w,"primary")?"true":"false");}
            if(const auto* label=static_cast<const std::string*>(g_object_get_data(G_OBJECT(w),"mc-accessible-source")))out+=",\"accessible_label\":"+mc::audit::quote(*label);
            if(GTK_IS_SPIN_BUTTON(w))out+=",\"value\":"+std::to_string(gtk_spin_button_get_value(GTK_SPIN_BUTTON(w)))+",\"text\":"+mc::audit::quote(gtk_editable_get_text(GTK_EDITABLE(w)));
            if(GTK_IS_CHECK_BUTTON(w))out+=",\"checked\":"+std::string(gtk_check_button_get_active(GTK_CHECK_BUTTON(w))?"true":"false");out+='}';
        }
        for(auto* c=gtk_widget_get_first_child(w);c;c=gtk_widget_get_next_sibling(c))self(self,c);
    };visit(visit,parent);return out+"]";
}
}
void auditControl(GtkWidget* w){
    auditWidgetId(w);if(g_object_get_data(G_OBJECT(w),"mc-audit-wired"))return;g_object_set_data(G_OBJECT(w),"mc-audit-wired",GINT_TO_POINTER(1));
    const auto changed=+[](GtkWidget* widget,gpointer){auto* root=gtk_widget_get_root(widget);auto* focus=root?gtk_root_get_focus(root):nullptr;
        if(!focus||(focus!=widget&&!gtk_widget_is_ancestor(focus,widget)))return;
        const auto id=mc::audit::input("gtk.control","control_value_changed","\"control_id\":"+std::to_string(auditWidgetId(widget))+",\"controls\":"+auditGtkState(gtk_widget_get_parent(widget)));
        g_object_set_data(G_OBJECT(widget),"mc-audit-input",reinterpret_cast<gpointer>(static_cast<std::uintptr_t>(id)));
    };
    const char* signal=GTK_IS_SPIN_BUTTON(w)?"value-changed":GTK_IS_CHECK_BUTTON(w)?"toggled":nullptr;
    if(signal){g_signal_connect(w,signal,G_CALLBACK(changed),nullptr);
        g_signal_connect_after(w,signal,G_CALLBACK(+[](GtkWidget* widget,gpointer){mc::audit::clearInput(reinterpret_cast<std::uintptr_t>(g_object_get_data(G_OBJECT(widget),"mc-audit-input")));}),nullptr);}
}

GtkWidget* actionButton(const char* caption, std::function<void()> callback, bool primary, bool enabled) {
    GtkWidget* button = gtk_button_new_with_label(caption);
    applyWidgetLanguage(button);auditControl(button);
    gtk_widget_add_css_class(button, "action");
    if (primary) gtk_widget_add_css_class(button, "primary");
    gtk_widget_set_sensitive(button, enabled);
    g_signal_connect(button,"clicked",G_CALLBACK(+[](GtkButton* button,gpointer){
        const char* caption=gtk_button_get_label(button);
        const auto* label=static_cast<const std::string*>(g_object_get_data(G_OBJECT(button),"mc-accessible-source"));
        const auto id=mc::audit::input("gtk.button","button_clicked","\"control_id\":"+std::to_string(auditWidgetId(GTK_WIDGET(button)))+",\"caption\":"+mc::audit::quote(caption?caption:"palette/custom")+",\"accessible_label\":"+mc::audit::quote(label?*label:"")+",\"controls_before\":"+auditGtkState(gtk_widget_get_parent(GTK_WIDGET(button))));
        g_object_set_data(G_OBJECT(button),"mc-audit-input",reinterpret_cast<gpointer>(static_cast<std::uintptr_t>(id)));
    }),nullptr);
    g_signal_connect_after(button,"clicked",G_CALLBACK(+[](GtkButton* button,gpointer){mc::audit::clearInput(reinterpret_cast<std::uintptr_t>(g_object_get_data(G_OBJECT(button),"mc-audit-input")));}),nullptr);
    GtkWidget* child = gtk_button_get_child(GTK_BUTTON(button));
    if (GTK_IS_LABEL(child)) gtk_label_set_ellipsize(GTK_LABEL(child), PANGO_ELLIPSIZE_END);
    if (callback) {
        auto* call = new std::function<void()>(std::move(callback));
        g_object_set_data_full(G_OBJECT(button), "magiccontrol-click", call,
            [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
        g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
            g_object_ref(button);struct Hold{GtkButton* p;~Hold(){g_object_unref(p);}} hold{button};
            mc::audit::Scope trace("gtk.dispatch","\"control_id\":"+std::to_string(auditWidgetId(GTK_WIDGET(button))),mc::audit::capture());
            (*static_cast<std::function<void()>*>(data))();
            trace.finish("callback_returned","\"controls_after\":"+auditGtkState(gtk_widget_get_parent(GTK_WIDGET(button))));
        }), call);
    }
    return button;
}
GtkWidget* scrolledWidget(GtkWidget* content) {
    // Windows parity: full-width content, opaque fixed right-edge scrollbar.
    // Never reserve a second gutter or use GTK's fading overlay scrollbar.
    GtkWidget* scroller=gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),GTK_POLICY_NEVER,GTK_POLICY_EXTERNAL);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroller),FALSE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller),FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller),content);
    auto* adjustment=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller));
    GtkWidget* bar=gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL,adjustment);
    gtk_widget_set_halign(bar,GTK_ALIGN_END);gtk_widget_set_valign(bar,GTK_ALIGN_FILL);
    gtk_widget_set_size_request(bar,12,-1);gtk_widget_add_css_class(bar,"fixed-scrollbar");
    GtkWidget* overlay=gtk_overlay_new();gtk_overlay_set_child(GTK_OVERLAY(overlay),scroller);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay),bar);
    g_signal_connect_object(adjustment,"changed",G_CALLBACK(+[](GtkAdjustment* a,gpointer p){
        gtk_widget_set_visible(GTK_WIDGET(p),gtk_adjustment_get_upper(a)>gtk_adjustment_get_page_size(a));
    }),bar,G_CONNECT_DEFAULT);
    gtk_widget_set_hexpand(overlay,TRUE);gtk_widget_set_vexpand(overlay,TRUE);
    return overlay;
}
GtkWidget* scrolledPage(Canvas* canvas) { return scrolledWidget(canvas->widget()); }

void installUiCss(GtkWidget* reference) {
    GdkDisplay* display=reference?gtk_widget_get_display(reference):gdk_display_get_default();
    if(!display)return;
    // The display owns one provider for the complete application lifetime.
    // Reload it on language/font/scale changes instead of stacking providers.
    struct CssState {GtkCssProvider* provider{};std::string family;double scale{};int dpi{};unsigned language{~0u};GWeakRef reference;
        CssState(){g_weak_ref_init(&reference,nullptr);}
        ~CssState(){g_weak_ref_clear(&reference);if(provider)g_object_unref(provider);}};
    auto* cached=static_cast<CssState*>(g_object_get_data(G_OBJECT(display),"mc-ui-css"));
    if(!cached){cached=new CssState;cached->provider=gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(display,GTK_STYLE_PROVIDER(cached->provider),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_set_data_full(G_OBJECT(display),"mc-ui-css",cached,[](gpointer p){delete static_cast<CssState*>(p);});
    }
    struct ReferenceOwner{GObject* value{};~ReferenceOwner(){if(value)g_object_unref(value);}} referenceOwner;
    if(reference)g_weak_ref_set(&cached->reference,G_OBJECT(reference));
    else{referenceOwner.value=G_OBJECT(g_weak_ref_get(&cached->reference));if(referenceOwner.value)reference=GTK_WIDGET(referenceOwner.value);}
    const double scale = displayScale(reference);
    const int dpi = displayDpi(reference);
    const auto px = [=](Font role) { return ui::fontHeight(role, dpi) / scale; };
    const std::string family = uiFamily();
    watchNewWindows();
    auto* windows=gtk_window_get_toplevels();
    for(guint i=0;i<g_list_model_get_n_items(windows);++i){auto* window=GTK_WIDGET(g_list_model_get_item(windows,i));if(!window)continue;
        if(gtk_widget_get_display(window)==display)updateWindowTypography(window,family);g_object_unref(window);}
    if(cached->family==family&&cached->scale==scale&&cached->dpi==dpi&&cached->language==ui::languageIndex())return;
    cached->family=family;cached->scale=scale;cached->dpi=dpi;cached->language=ui::languageIndex();
    const auto cssFamily=cssFontFamily(family);
    std::ostringstream css;
    css << "* {font-family: " << cssFamily << "; font-size:" << px(Font::Body) << "px;}\n"
        << "menubutton.chrome {min-width:0;min-height:0;padding:0;margin:0;background:transparent;}\n"
        << "box.language-menu {background:" << cssColor(theme::kSurface)
        << ";color:" << cssColor(theme::kTextPrimary) << ";border:"
        << mc::gui::kLanguageMenuBorderWidth << "px solid " << cssColor(theme::kDivider)
        << ";border-radius:0;box-shadow:none;padding:0;}\n"
        << "button.language-option {min-width:0;min-height:" << mc::gui::kLanguageMenuRowHeight
        << "px;padding:0;margin:0;border:0;border-radius:0;box-shadow:none;background:transparent;color:"
        << cssColor(theme::kTextPrimary) << ";}\n"
        << "button.language-option:hover,button.language-option:active {background:"
        << cssColor(theme::kHoverOverlay) << ";}\n"
        << "label.language-option-label {font-family:" << cssFamily << ";font-size:"
        << px(Font::Body) << "px;font-weight:400;color:" << cssColor(theme::kTextPrimary) << ";}\n"
        << "window, window.csd {background:" << cssColor(theme::kBackground)
        << ";color:" << cssColor(theme::kTextPrimary) << ";border-radius:0;box-shadow:none;border:0;}\n"
        << "scrolledwindow,viewport {background:" << cssColor(theme::kBackground) << ";}\n"
        << "windowhandle.magiccontrol-titlebar {min-height:0;padding:0;margin:0;border:0;background:transparent;}\n"
        << "button {min-width:0;min-height:0;padding:0;border-radius:0;box-shadow:none;outline:none;}\n"
        << "button.action {background:" << cssColor(theme::kSurfaceRaised) << ";color:"
        << cssColor(theme::kTextPrimary) << ";border:" << 1.0/scale << "px solid "
        << cssColor(theme::kDivider) << ";}\n"
        << "button.action:hover {background:" << cssColor(theme::kHoverOverlay) << ";}\n"
        << "button.action:active {background:" << cssColor(theme::kAccentPressed) << ";}\n"
        << "button.primary,button.action:checked {background:" << cssColor(theme::kAccent) << ";}\n"
        << "button.action.primary:disabled {background:" << cssColor(theme::kAccent)
        << ";color:" << cssColor(theme::kTextPrimary) << ";opacity:1;}\n"
        << "button.action:disabled {background:" << cssColor(theme::kSurface) << ";color:"
        << cssColor(theme::kTextMuted) << ";opacity:1;}\n"
        << "button.curve-preset {font-size:" << px(Font::Caption) << "px;}\n"
        << "button.chrome,button.chrome:hover,button.chrome:active {background:transparent;border:0;}\n"
        << "scale.discrete-slider {min-height:0;padding:0 6px;margin:0;}\n"
        << "scale.discrete-slider marks {min-height:3px;}\n"
        << "scale.discrete-slider marks indicator {min-width:1px;min-height:3px;background:" << cssColor(theme::kTextSecondary) << ";}\n"
        << "scale.discrete-slider:disabled marks indicator {background:" << cssColor(theme::kTextMuted) << ";}\n"
        << "scale.discrete-slider trough {min-height:4px;background:" << cssColor(theme::kNumericBorder) << ";border:0;}\n"
        << "scale.discrete-slider highlight {background:" << cssColor(theme::kAccent) << ";border:0;}\n"
        << "scale.discrete-slider slider {min-width:12px;min-height:18px;margin:-7px 0;border:0;border-radius:0;background:" << cssColor(theme::kAccent) << ";box-shadow:none;}\n"
        << "scale.discrete-slider slider:hover,scale.discrete-slider:focus slider {background:" << cssColor(theme::kAccentHover) << ";}\n"
        << "scale.discrete-slider slider:active {background:" << cssColor(theme::kAccentPressed) << ";}\n"
        << "scale.discrete-slider:disabled slider,scale.discrete-slider:disabled highlight {background:" << cssColor(theme::kTextMuted) << ";}\n"
        << "checkbutton.keyboard-auto-off {min-width:0;min-height:0;padding:0;margin:0;}\n"
        << "checkbutton.keyboard-auto-off check {min-width:"
        << mc::gui::kKeyboardTimeoutControlSize - 2 * mc::gui::kKeyboardTimeoutCheckBorder
        << "px;min-height:" << mc::gui::kKeyboardTimeoutControlSize - 2 * mc::gui::kKeyboardTimeoutCheckBorder
        << "px;padding:0;margin:0;border-radius:0;border:" << mc::gui::kKeyboardTimeoutCheckBorder
        << "px solid " << cssColor(theme::kNumericBorder)
        << ";background:" << cssColor(theme::kSurfaceRaised) << ";}\n"
        << "checkbutton.keyboard-auto-off check:checked {background:" << cssColor(theme::kAccent)
        << ";color:" << cssColor(theme::kTextPrimary) << ";}\n"
        << "spinbutton.numeric-field {min-width:0;min-height:0;padding:0;border-radius:0;"
        << "background:" << cssColor(theme::kNumericBackground) << ";color:"
        << cssColor(theme::kNumericText) << ";border:1px solid " << cssColor(theme::kNumericBorder) << ";}\n"
        << ".numeric-field, .numeric-field text, .numeric-field button {font-size:"
        << px(Font::Caption) << "px;}\n"
        << "spinbutton.numeric-field text {min-width:0;padding:0;outline:none;}\n"
        << "spinbutton.numeric-field button {min-width:0;min-height:0;padding:0;margin:0;"
        << "background:" << cssColor(theme::kNumericBackground) << ";color:"
        << cssColor(theme::kNumericText) << ";border:"
        << 1.0/scale << "px solid " << cssColor(theme::kNumericButtonBorder) << ";}\n"
        << "spinbutton.numeric-field button:hover {background:" << cssColor(theme::kNumericHover) << ";}\n"
        << "spinbutton.numeric-field button:active {background:" << cssColor(theme::kNumericPressed) << ";}\n"
        << "spinbutton.numeric-field button:disabled {color:" << cssColor(theme::kNumericDisabledText) << ";}\n"
        << "spinbutton.numeric-field button image {-gtk-icon-size:8px;}\n"
        // Padding is part of GTK's size request: a 12 px minimum plus 6 px
        // padding would make an 18 px gutter and shift the thumb inward.
        << "scrollbar.vertical {min-width:0;background:transparent;border:0;padding:"
        << mc::gui::kScrollBarTrackInset << "px "
        << (mc::gui::kScrollBarTrackWidth-mc::gui::kScrollBarThumbWidth)/2.0 << "px;}\n"
        << "scrollbar.vertical trough {min-width:" << mc::gui::kScrollBarThumbWidth
        << "px;padding:0;border:0;background:transparent;}\n"
        << "scrollbar slider {min-width:" << mc::gui::kScrollBarThumbWidth
        << "px;min-height:" << mc::gui::kScrollBarThumbMinHeight
        << "px;margin:0;border:0;border-radius:8px;"
        << "background:" << cssColor(theme::kScrollThumb) << ";}\n"
        << "scrollbar.fixed-scrollbar {background:" << cssColor(theme::kBackground) << ";opacity:1;}\n"
        << "scrollbar slider:active {min-width:" << mc::gui::kScrollBarThumbWidthHot
        << "px;background:" << cssColor(theme::kScrollThumbDragging) << ";}\n"
        << "textview, textview text {background:" << cssColor(theme::kSurface) << ";color:" << cssColor(theme::kTextPrimary) << ";}\n"
        << "button.credit-button {background:transparent;border:0;padding:0;min-height:0;}\n"
        << "button.about-link {color:" << cssColor(theme::kAccentHover) << ";}\n"
        << "headerbar {background:" << cssColor(theme::kTitleBar) << ";color:" << cssColor(theme::kTextPrimary) << ";border:0;box-shadow:none;}\n"
        // License uses a single surface, including the external scrollbar gutter.
        << "window.license-window, .license-window scrolledwindow, .license-window viewport, .license-window textview, .license-window textview text, .license-window scrollbar.fixed-scrollbar {background:" << cssColor(theme::kSurface) << ";}\n"
        << ".license-window headerbar {min-height:32px;padding:0 6px;margin:0;}\n"
        << ".license-window headerbar button {min-width:28px;min-height:28px;padding:0;color:" << cssColor(theme::kTextPrimary) << ";}\n"
        << ".license-window headerbar button image {color:" << cssColor(theme::kTextPrimary) << ";opacity:1;}\n"
        << "label.selectable-data {font-size:" << px(Font::ClassicBody) << "px;color:" << cssColor(theme::kTextPrimary) << ";}\n"
        << "label.about-heading {font-size:" << px(Font::ClassicHeading) << "px;font-weight:700;}\n"
        << ".heading-action, .heading-action > button {padding:0;margin:0;border:0;background:transparent;color:" << cssColor(theme::kTextPrimary) << ";}\n"
        << ".heading-action label {font-size:" << px(Font::ClassicHeading) << "px;font-weight:700;}\n"
        << ".heading-action:focus-visible, .heading-action > button:focus-visible {outline:none;box-shadow:none;}\n"
        << ".language-menu separator {min-height:1px;min-width:0;padding:0;border:0;opacity:1;background:" << cssColor(theme::kDivider) << ";}\n"
        << "button.selected-language label {color:" << cssColor(theme::kAccentHover) << ";}\n"
        << "label.about-secondary {font-size:" << px(Font::KeyDetail) << "px;color:" << cssColor(theme::kTextSecondary) << ";}\n"
        << "label.about-detail {font-size:" << px(Font::KeyDetail) << "px;color:" << cssColor(theme::kTextMuted) << ";}\n"
        << "label.about-detail.donation-copied {color:" << cssColor(theme::kBatteryCharging) << ";}\n"
        << "button.credit-button:hover {background:" << cssColor(theme::kHoverOverlay) << ";}\n"
        << "scrollbar:hover slider {min-width:" << mc::gui::kScrollBarThumbWidthHot
        << "px;background:" << cssColor(theme::kScrollThumbHot) << ";}\n";
    gtk_css_provider_load_from_string(cached->provider, css.str().c_str());
    g_print("MagicControl GTK typography: bundled-family=%s, dpi=%d, body=%.2fpx, caption=%.2fpx\n",
        family.c_str(), dpi, px(Font::Body), px(Font::Caption));
}


void showOperationDialog(GtkWindow* parent,const std::string& title,std::shared_ptr<OperationMessages> messages,std::function<void()> cancel){
    struct View {
        GtkWindow* window{};GtkWidget *text{},*button{};std::shared_ptr<OperationMessages> messages;std::function<void()> cancel;
        std::size_t cursor{};guint timer{};bool cancelSent{};
        ~View(){if(timer)g_source_remove(timer);}
        void close(){if(!messages->done()){
            if(!cancelSent){cancelSent=true;messages->append("Запрошена отмена. Ожидание безопасного завершения текущего шага.");if(cancel)cancel();}return;
        }gtk_window_destroy(window);}
        void pump(){
            auto* adjustment=gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(text));
            const bool follow=!adjustment||gtk_adjustment_get_value(adjustment)+gtk_adjustment_get_page_size(adjustment)>=gtk_adjustment_get_upper(adjustment)-2;
            auto batch=messages->read(cursor);
            if(!batch.empty()){
                auto* buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));GtkTextIter end;gtk_text_buffer_get_end_iter(buffer,&end);
                for(const auto& row:batch){const auto text=row.text+"\n";
                    const auto* tag=row.tone==OperationMessages::Tone::Error?"operation-error":row.tone==OperationMessages::Tone::Success?"operation-success":nullptr;
                    if(tag)gtk_text_buffer_insert_with_tags_by_name(buffer,&end,text.c_str(),static_cast<int>(text.size()),tag,nullptr);
                    else gtk_text_buffer_insert(buffer,&end,text.c_str(),static_cast<int>(text.size()));}
                if(follow){auto* mark=gtk_text_buffer_get_insert(buffer);gtk_text_buffer_place_cursor(buffer,&end);gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text),mark,0,FALSE,0,0);}
            }
            gtk_button_set_label(GTK_BUTTON(button),ui::translate(messages->done()?"Закрыть":cancelSent?"Отмена…":"Отмена").c_str());
        }
    };
    auto* state=new View;state->messages=std::move(messages);state->cancel=std::move(cancel);
    state->window=GTK_WINDOW(gtk_window_new());setWindowTitle(state->window,title.c_str());
    gtk_window_set_transient_for(state->window,parent);gtk_window_set_destroy_with_parent(state->window,TRUE);gtk_window_set_default_size(state->window,780,440);
    gtk_widget_add_css_class(GTK_WIDGET(state->window),"license-window");
    auto* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    for(auto setter:{gtk_widget_set_margin_start,gtk_widget_set_margin_end,gtk_widget_set_margin_top,gtk_widget_set_margin_bottom})setter(root,16);
    state->text=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(state->text),FALSE);gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(state->text),FALSE);
    auto* buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->text));
    gtk_text_buffer_create_tag(buffer,"operation-error","foreground",cssColor(theme::kError).c_str(),nullptr);
    gtk_text_buffer_create_tag(buffer,"operation-success","foreground",cssColor(theme::kOk).c_str(),nullptr);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(state->text),GTK_WRAP_WORD_CHAR);
    auto* scroll=scrolledWidget(state->text);gtk_widget_set_vexpand(scroll,TRUE);gtk_widget_set_hexpand(scroll,TRUE);gtk_box_append(GTK_BOX(root),scroll);
    state->button=actionButton("Отмена",[state]{state->close();});gtk_widget_set_halign(state->button,GTK_ALIGN_END);gtk_box_append(GTK_BOX(root),state->button);
    gtk_window_set_child(state->window,root);installUiCss(GTK_WIDGET(state->window));
    g_signal_connect(state->window,"close-request",G_CALLBACK(+[](GtkWindow*,gpointer p)->gboolean{static_cast<View*>(p)->close();return TRUE;}),state);
    g_object_set_data_full(G_OBJECT(state->window),"operation-view",state,[](gpointer p){delete static_cast<View*>(p);});
    state->timer=g_timeout_add(80,+[](gpointer p)->gboolean{static_cast<View*>(p)->pump();return G_SOURCE_CONTINUE;},state);
    state->pump();gtk_window_present(state->window);
}
int showChoiceDialog(GtkWindow* parent,const std::string& message,const std::vector<std::string>& choices){
    if(choices.empty()||choices.size()>3)return -1;
    struct Choice{GtkWindow* window{};GMainLoop* loop{};int answer{-1};} state;
    state.loop=g_main_loop_new(nullptr,FALSE);state.window=GTK_WINDOW(gtk_window_new());g_object_ref_sink(state.window);
    setWindowTitle(state.window,"MagicControl");gtk_widget_add_css_class(GTK_WIDGET(state.window),"license-window");
    gtk_window_set_transient_for(state.window,parent);gtk_window_set_modal(state.window,TRUE);gtk_window_set_destroy_with_parent(state.window,TRUE);
    gtk_window_set_resizable(state.window,TRUE);gtk_window_set_default_size(state.window,780,300);
    auto* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,16);
    for(auto setter:{gtk_widget_set_margin_start,gtk_widget_set_margin_end,gtk_widget_set_margin_top,gtk_widget_set_margin_bottom})setter(root,16);
    auto* label=gtk_label_new(message.c_str());gtk_label_set_wrap(GTK_LABEL(label),TRUE);gtk_label_set_wrap_mode(GTK_LABEL(label),PANGO_WRAP_WORD_CHAR);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_label_set_selectable(GTK_LABEL(label),TRUE);
    auto* scroll=scrolledWidget(label);gtk_widget_set_vexpand(scroll,TRUE);gtk_box_append(GTK_BOX(root),scroll);
    auto* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);GtkWidget* last{};
    for(std::size_t i=0;i<choices.size();++i){auto* button=actionButton(choices[i].c_str(),[&state,i]{state.answer=static_cast<int>(i);gtk_widget_set_visible(GTK_WIDGET(state.window),FALSE);g_main_loop_quit(state.loop);});
        gtk_widget_set_hexpand(button,TRUE);gtk_box_append(GTK_BOX(row),button);last=button;}
    gtk_box_append(GTK_BOX(root),row);gtk_window_set_child(state.window,root);installUiCss(GTK_WIDGET(state.window));
    g_signal_connect(state.window,"close-request",G_CALLBACK(+[](GtkWindow* w,gpointer p)->gboolean{auto* s=static_cast<Choice*>(p);gtk_widget_set_visible(GTK_WIDGET(w),FALSE);g_main_loop_quit(s->loop);return TRUE;}),&state);
    g_signal_connect(state.window,"notify::visible",G_CALLBACK(+[](GObject* w,GParamSpec*,gpointer p){if(!gtk_widget_get_visible(GTK_WIDGET(w)))g_main_loop_quit(static_cast<Choice*>(p)->loop);}),&state);
    auto* keys=gtk_event_controller_key_new();g_signal_connect(keys,"key-pressed",G_CALLBACK(+[](GtkEventControllerKey*,guint key,guint,GdkModifierType,gpointer p)->gboolean{
        if(key!=GDK_KEY_Escape)return FALSE;auto* s=static_cast<Choice*>(p);gtk_widget_set_visible(GTK_WIDGET(s->window),FALSE);g_main_loop_quit(s->loop);return TRUE;}),&state);
    gtk_widget_add_controller(GTK_WIDGET(state.window),GTK_EVENT_CONTROLLER(keys));gtk_window_present(state.window);if(last)gtk_widget_grab_focus(last);g_main_loop_run(state.loop);
    gtk_window_destroy(state.window);g_object_unref(state.window);g_main_loop_unref(state.loop);return state.answer;
}

}  // namespace mc::gtkui
