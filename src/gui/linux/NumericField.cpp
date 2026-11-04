#include "Ui.h"
#include "NumericField.h"
#include "../common/EcByteEditor.h"

#include <utility>
#include <vector>

namespace mc::gtkui {

namespace {

struct NativeFieldParts { GtkWidget* up; GtkWidget* down; int arrowWidth{-1}; };

struct NativeScrollControllers {
    std::vector<std::pair<GtkEventController*, GtkPropagationPhase>> controllers;

    ~NativeScrollControllers() {
        for (const auto& [controller, phase] : controllers) {
            (void)phase;
            g_object_unref(controller);
        }
    }
};

void updateWheelFocus(GObject* object, GParamSpec*, gpointer data) {
    const bool focused = gtk_event_controller_focus_contains_focus(
        GTK_EVENT_CONTROLLER_FOCUS(object));
    const auto* native = static_cast<const NativeScrollControllers*>(data);
    for (const auto& [controller, phase] : native->controllers) {
        gtk_event_controller_set_propagation_phase(controller,
            focused ? phase : GTK_PHASE_NONE);
    }
}

void installWheelFocusGate(GtkWidget* spin) {
    auto* native = new NativeScrollControllers();
    GListModel* controllers = gtk_widget_observe_controllers(spin);
    for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i) {
        auto* controller = GTK_EVENT_CONTROLLER(g_list_model_get_item(controllers, i));
        if (GTK_IS_EVENT_CONTROLLER_SCROLL(controller)) {
            native->controllers.emplace_back(controller,
                gtk_event_controller_get_propagation_phase(controller));
        } else {
            g_object_unref(controller);
        }
    }
    g_object_unref(controllers);

    GtkEventController* focus = gtk_event_controller_focus_new();
    g_object_set_data_full(G_OBJECT(focus), "magiccontrol-native-scroll", native,
        [](gpointer value) { delete static_cast<NativeScrollControllers*>(value); });
    g_signal_connect(focus, "notify::contains-focus", G_CALLBACK(updateWheelFocus), native);
    gtk_widget_add_controller(spin, focus);
    // Disabling the native numeric scroll handler leaves the event available
    // to GtkScrolledWindow, including smooth touchpad scrolling and momentum.
    updateWheelFocus(G_OBJECT(focus), nullptr, native);
}

}  // namespace

void limitWheelToFocusedField(GtkWidget* widget){installWheelFocusGate(widget);}
void installNumericFocusCommit(GtkWidget* scope){
    auto* click=gtk_gesture_click_new();gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click),0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),GTK_PHASE_CAPTURE);
    g_signal_connect(click,"pressed",G_CALLBACK(+[](GtkGestureClick*,int,double x,double y,gpointer p){
        auto* page=GTK_WIDGET(p);auto* root=gtk_widget_get_root(page);if(!root)return;
        auto* focus=gtk_root_get_focus(root);auto* field=focus?gtk_widget_get_ancestor(focus,GTK_TYPE_SPIN_BUTTON):nullptr;
        if(!field||!gtk_widget_is_ancestor(field,page))return;
        auto* target=gtk_widget_pick(page,x,y,GTK_PICK_DEFAULT);
        auto* targetField=target?gtk_widget_get_ancestor(target,GTK_TYPE_SPIN_BUTTON):nullptr;
        if(targetField!=field)gtk_root_set_focus(root,nullptr);
    }),scope);gtk_widget_add_controller(scope,GTK_EVENT_CONTROLLER(click));
    auto* keys=gtk_event_controller_key_new();gtk_event_controller_set_propagation_phase(keys,GTK_PHASE_CAPTURE);
    g_signal_connect(keys,"key-pressed",G_CALLBACK(+[](GtkEventControllerKey*,guint key,guint,GdkModifierType,gpointer p)->gboolean{
        if(key!=GDK_KEY_Return&&key!=GDK_KEY_KP_Enter)return FALSE;
        auto* page=GTK_WIDGET(p);auto* root=gtk_widget_get_root(page);if(!root)return FALSE;
        auto* focus=gtk_root_get_focus(root);auto* field=focus?gtk_widget_get_ancestor(focus,GTK_TYPE_SPIN_BUTTON):nullptr;
        if(!field||!gtk_widget_is_ancestor(field,page))return FALSE;
        if(g_object_get_data(G_OBJECT(field),"mc-enter-only"))return FALSE;
        gtk_root_set_focus(root,nullptr);return TRUE;
    }),scope);gtk_widget_add_controller(scope,keys);
}

GtkWidget* createNumericField(double minimum, double maximum, double step,bool hexadecimal) {
    GtkWidget* spin = gtk_spin_button_new_with_range(minimum, maximum, step);
    auditControl(spin);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), !hexadecimal);
    if(hexadecimal){
        g_signal_connect(spin,"input",G_CALLBACK(+[](GtkSpinButton* button,gdouble* value,gpointer)->gint{
            const auto parsed=mc::gui::ui::parseEcHex(gtk_editable_get_text(GTK_EDITABLE(button)),0xffff);
            if(!parsed)return GTK_INPUT_ERROR;*value=*parsed;return TRUE;
        }),nullptr);
        g_signal_connect(spin,"output",G_CALLBACK(+[](GtkSpinButton* button,gpointer)->gboolean{
            const auto text=std::format("0x{:02X}",gtk_spin_button_get_value_as_int(button));gtk_editable_set_text(GTK_EDITABLE(button),text.c_str());return TRUE;
        }),nullptr);
    }
    gtk_editable_set_width_chars(GTK_EDITABLE(spin), 0);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(spin), hexadecimal?6:3);
    gtk_editable_set_alignment(GTK_EDITABLE(spin), 0.5F);
    gtk_widget_set_hexpand(spin, TRUE);
    gtk_widget_set_size_request(spin, -1, -1);
    gtk_widget_add_css_class(spin, "numeric-field");
    limitWheelToFocusedField(spin);

    GtkWidget* text = nullptr;
    GtkWidget* up = nullptr;
    GtkWidget* down = nullptr;
    for (GtkWidget* child = gtk_widget_get_first_child(spin); child != nullptr;
         child = gtk_widget_get_next_sibling(child)) {
        if (GTK_IS_TEXT(child)) text = child;
        else if (GTK_IS_BUTTON(child) && gtk_widget_has_css_class(child, "up")) up = child;
        else if (GTK_IS_BUTTON(child) && gtk_widget_has_css_class(child, "down")) down = child;
    }
    if (text == nullptr || up == nullptr || down == nullptr) {
        g_warning("GtkSpinButton child structure changed; using its default layout");
        return spin;
    }

    // Keep GTK's own text, buttons, repeat gestures, validation and adjustment.
    // Only their layout changes: the text spans two rows, arrows sit to its right.
    GtkLayoutManager* layout = gtk_grid_layout_new();
    gtk_grid_layout_set_row_homogeneous(GTK_GRID_LAYOUT(layout), TRUE);
    gtk_widget_set_layout_manager(spin, layout);
    auto place = [layout](GtkWidget* child, int column, int row, int rowSpan) {
        auto* cell = GTK_GRID_LAYOUT_CHILD(
            gtk_layout_manager_get_layout_child(layout, child));
        gtk_grid_layout_child_set_column(cell, column);
        gtk_grid_layout_child_set_row(cell, row);
        gtk_grid_layout_child_set_row_span(cell, rowSpan);
    };
    place(text, 0, 0, 2);
    place(up, 1, 0, 1);
    place(down, 1, 1, 1);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_button_set_icon_name(GTK_BUTTON(up), "pan-up-symbolic");
    gtk_button_set_icon_name(GTK_BUTTON(down), "pan-down-symbolic");
    g_object_set_data_full(G_OBJECT(spin), "magiccontrol-field-parts",
        new NativeFieldParts{up, down}, [](gpointer value) { delete static_cast<NativeFieldParts*>(value); });
    return spin;
}

void setNumericArrowWidth(GtkWidget* spin, int logicalWidth) {
    auto* parts = static_cast<NativeFieldParts*>(g_object_get_data(G_OBJECT(spin), "magiccontrol-field-parts"));
    if (!parts || parts->arrowWidth == logicalWidth) return;
    parts->arrowWidth = logicalWidth;
    gtk_widget_set_size_request(parts->up, logicalWidth, -1);
    gtk_widget_set_size_request(parts->down, logicalWidth, -1);
}

}  // namespace mc::gtkui
