#include "UpdateWindow.h"
#include "Ui.h"
#include "MagicControl/AppUpdate.h"
#include "MagicControl/Version.h"
#include "../common/UpdatePresentation.h"

namespace mc::gtkui {
namespace {
struct State {
    GtkWindow* window{};GtkWidget* label{},*check{},*install{};guint timer{};
    update::Operations operations;std::string shown;
    std::function<void()> readyCallback;bool busy{},closing{};
    ~State(){if(timer)g_source_remove(timer);operations.shutdown();}
    void start(bool apply){
        if(busy||!operations.start(apply))return;
        busy=true;gtk_widget_set_sensitive(check,FALSE);gtk_widget_set_sensitive(install,FALSE);
    }
};
}
void showUpdateWindow(GtkWindow* parent,std::function<void()> callback){
    auto* state=new State;state->readyCallback=std::move(callback);state->window=GTK_WINDOW(gtk_window_new());
    setWindowTitle(state->window,"MagicControl — Обновления");gtk_window_set_icon_name(state->window,"com.mechrevo.magiccontrol");
    gtk_widget_add_css_class(GTK_WIDGET(state->window),"license-window");gtk_window_set_transient_for(state->window,parent);gtk_window_set_modal(state->window,TRUE);gtk_window_set_destroy_with_parent(state->window,TRUE);gtk_window_set_default_size(state->window,520,230);gtk_window_set_resizable(state->window,FALSE);
    g_object_set_data_full(G_OBJECT(state->window),"update-state",state,[](gpointer p){delete static_cast<State*>(p);});
    auto* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);for(auto setter:{gtk_widget_set_margin_start,gtk_widget_set_margin_end,gtk_widget_set_margin_top,gtk_widget_set_margin_bottom})setter(root,16);
    auto* version=gtk_label_new("MagicControl " MAGICCONTROL_VERSION_STRING);gtk_label_set_xalign(GTK_LABEL(version),0);gtk_box_append(GTK_BOX(root),version);
    state->label=gtk_label_new("Нажмите «Проверить».");gtk_label_set_wrap(GTK_LABEL(state->label),TRUE);gtk_label_set_xalign(GTK_LABEL(state->label),0);gtk_label_set_yalign(GTK_LABEL(state->label),0);gtk_widget_set_vexpand(state->label,TRUE);gtk_box_append(GTK_BOX(root),state->label);
    const auto previous=update::lastResult();if(!previous.empty())gtk_label_set_text(GTK_LABEL(state->label),previous.c_str());
    auto* buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);auto* github=actionButton("GitHub",[state]{auto* uri=gtk_uri_launcher_new("https://github.com/nikimagic/MagicControl/releases");gtk_uri_launcher_launch(uri,state->window,nullptr,nullptr,nullptr);g_object_unref(uri);});gtk_widget_set_size_request(github,80,30);gtk_box_append(GTK_BOX(buttons),github);
    auto* space=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_widget_set_hexpand(space,TRUE);gtk_box_append(GTK_BOX(buttons),space);
    state->check=actionButton("Проверить",[state]{state->start(false);});state->install=actionButton("Обновить",[state]{state->start(true);},false,false);
    auto* close=actionButton("Закрыть",[state]{gtk_window_close(state->window);});for(auto* b:{state->check,state->install,close}){gtk_widget_set_size_request(b,96,30);gtk_box_append(GTK_BOX(buttons),b);}gtk_box_append(GTK_BOX(root),buttons);
    g_signal_connect(state->window,"close-request",G_CALLBACK(+[](GtkWindow*,gpointer p)->gboolean{auto* s=static_cast<State*>(p);if(!s->busy)return FALSE;s->closing=true;s->operations.cancel();return TRUE;}),state);
    state->timer=g_timeout_add(80,+[](gpointer p)->gboolean{
        auto* s=static_cast<State*>(p);const auto result=s->operations.snapshot();
        const auto text=ui::updateMessage(result);const bool done=result.done,ready=result.ready;
        if(!text.empty()&&text!=s->shown){s->shown=text;gtk_label_set_text(GTK_LABEL(s->label),text.c_str());}
        if(s->busy&&done){s->busy=false;gtk_widget_set_sensitive(s->check,TRUE);gtk_widget_set_sensitive(s->install,result.release.available);}
        if(ready){auto callback=s->readyCallback;auto* window=s->window;s->timer=0;gtk_window_destroy(window);callback();return G_SOURCE_REMOVE;}
        if(s->closing&&done){s->timer=0;gtk_window_destroy(s->window);return G_SOURCE_REMOVE;}return G_SOURCE_CONTINUE;
    },state);
    auto* keys=gtk_event_controller_key_new();g_signal_connect(keys,"key-pressed",G_CALLBACK(+[](GtkEventControllerKey*,guint key,guint,GdkModifierType,gpointer p)->gboolean{if(key!=GDK_KEY_Escape)return FALSE;gtk_window_close(GTK_WINDOW(p));return TRUE;}),state->window);gtk_widget_add_controller(GTK_WIDGET(state->window),keys);
    gtk_window_set_child(state->window,root);applyWidgetLanguage(GTK_WIDGET(state->window));gtk_window_present(state->window);
}
}
