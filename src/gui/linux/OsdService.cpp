#include "OsdService.h"
#include "Ui.h"
#include "../common/OsdMessages.h"
#include "../common/OsdIcon.h"
#include "../../platform/linux/OsdSystemMonitor.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/KeysLifecycle.h"
#include "../../platform/linux/KeysControlLinux.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#ifdef MAGICCONTROL_LAYER_SHELL
#include <gtk4-layer-shell.h>
#endif

namespace mc::gtkui {
namespace {
namespace osd=mc::gui::osd;
const char* iconName(osd::Icon icon){
    switch(icon){
    case osd::Icon::Fan:return "preferences-system-power-symbolic";
    case osd::Icon::Keyboard:return "input-keyboard-symbolic";
    case osd::Icon::Sound:return "audio-volume-high-symbolic";
    case osd::Icon::Muted:return "audio-volume-muted-symbolic";
    case osd::Icon::Brightness:return "display-brightness-symbolic";
    case osd::Icon::Microphone:return "microphone-sensitivity-high-symbolic";
    case osd::Icon::Touchpad:return "input-touchpad-symbolic";
    case osd::Icon::Lock:return "changes-prevent-symbolic";
    case osd::Icon::Battery:return "battery-level-100-symbolic";
    case osd::Icon::Power:return "system-run-symbolic";
    case osd::Icon::Camera:return "camera-web-symbolic";
    case osd::Icon::Wifi:return "network-wireless-symbolic";
    case osd::Icon::Display:return "video-display-symbolic";
    case osd::Icon::Bluetooth:return "bluetooth-active-symbolic";
    case osd::Icon::Windows:return "input-keyboard-symbolic";
    case osd::Icon::Usb:return "drive-removable-media-usb-symbolic";
    case osd::Icon::Notifications:return "preferences-system-notifications-symbolic";
    default:return "dialog-information-symbolic";
    }
}
void color(cairo_t* cr,Color value){cairo_set_source_rgb(cr,(value&255)/255.0,((value>>8)&255)/255.0,((value>>16)&255)/255.0);}
void rounded(cairo_t* cr,int width,int height,int radius){
    constexpr double pi=3.141592653589793;
    cairo_new_sub_path(cr);cairo_arc(cr,width-radius,radius,radius,-pi/2,0);
    cairo_arc(cr,width-radius,height-radius,radius,0,pi/2);cairo_arc(cr,radius,height-radius,radius,pi/2,pi);
    cairo_arc(cr,radius,radius,radius,pi,pi*1.5);cairo_close_path(cr);
}
}
struct OsdService::Impl {
    GtkWindow* owner{};GtkOverlay* overlay{};GtkWindow* layer{};
    Canvas* card{};
    std::unique_ptr<mc::linuxplatform::OsdSystemMonitor> monitor;
    osd::State state;bool enabled{true},stopped{},layerSupported{},serverAvailable{},notifyBusy{},usingDesktop{};
    guint timer{},watch{},closedSubscription{};
    guint32 notificationId{};
    std::uint64_t nextToken{},lastRendered{},lastLogged{},lastPainted{},lastNotifyAt{};
    std::optional<osd::Message> pendingNotice;
    GDBusConnection* session{};GCancellable* cancel{g_cancellable_new()};
    struct Life{Impl* owner;};std::shared_ptr<Life> life=std::make_shared<Life>(Life{this});

    ~Impl(){stop();g_object_unref(cancel);}
    void initialize(GtkWindow* window,GtkOverlay* parent){
        owner=window;overlay=parent;
        bool disabled{};std::string error;
        if(mc::linux_platform::loadApplicationKeyPreferences(disabled,error))enabled=!disabled;
        card=Canvas::create([this](Canvas& c,int,int){
            const ui::Scale s{c.dpi()};return s(osd::kHeight);
        },[this](Canvas& c,cairo_t* cr,int width,int height){paint(c,cr,width,height);},false,osd::kHeight);
        gtk_widget_set_hexpand(card->widget(),FALSE);gtk_widget_set_size_request(card->widget(),osd::kWidth,osd::kHeight);
        gtk_widget_set_can_target(card->widget(),FALSE);gtk_widget_set_focusable(card->widget(),FALSE);
        GtkCssProvider* css=gtk_css_provider_new();
        gtk_css_provider_load_from_string(css,".magiccontrol-osd-icon {color:#a090ff;} .magiccontrol-osd-icon.error {color:#ff9595;} window.magiccontrol-osd {background:transparent;border:0;box-shadow:none;}");
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);g_object_unref(css);
#ifdef MAGICCONTROL_LAYER_SHELL
        layerSupported=gtk_layer_is_supported();
        if(layerSupported){
            layer=GTK_WINDOW(gtk_window_new());gtk_window_set_decorated(layer,FALSE);gtk_window_set_resizable(layer,FALSE);
            gtk_widget_add_css_class(GTK_WIDGET(layer),"magiccontrol-osd");setWindowTitle(layer,"MagicControl OSD");
            gtk_layer_init_for_window(layer);gtk_layer_set_namespace(layer,"magiccontrol-osd");
            gtk_layer_set_layer(layer,GTK_LAYER_SHELL_LAYER_OVERLAY);gtk_layer_set_keyboard_mode(layer,GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
            gtk_layer_set_exclusive_zone(layer,0);gtk_layer_set_anchor(layer,GTK_LAYER_SHELL_EDGE_LEFT,TRUE);gtk_layer_set_anchor(layer,GTK_LAYER_SHELL_EDGE_TOP,TRUE);
            gtk_window_set_child(layer,card->widget());
            g_signal_connect(layer,"realize",G_CALLBACK(+[](GtkWidget* widget,gpointer){
                GdkSurface* surface=gtk_native_get_surface(GTK_NATIVE(widget));cairo_region_t* empty=cairo_region_create();
                gdk_surface_set_input_region(surface,empty);cairo_region_destroy(empty);
            }),nullptr);
        } else
#endif
        {
            gtk_widget_set_halign(card->widget(),GTK_ALIGN_START);gtk_widget_set_valign(card->widget(),GTK_ALIGN_START);
            gtk_overlay_add_overlay(overlay,card->widget());
        }
        g_object_ref(card->widget());gtk_widget_set_visible(card->widget(),FALSE);
        GError* busError{};session=g_bus_get_sync(G_BUS_TYPE_SESSION,cancel,&busError);if(busError)g_error_free(busError);
        if(session){
            watch=g_bus_watch_name_on_connection(session,"org.freedesktop.Notifications",G_BUS_NAME_WATCHER_FLAGS_NONE,
                +[](GDBusConnection*,const gchar*,const gchar*,gpointer data){static_cast<Impl*>(data)->serverAvailable=true;},
                +[](GDBusConnection*,const gchar*,gpointer data){auto* self=static_cast<Impl*>(data);self->serverAvailable=false;self->notificationId=0;},this,nullptr);
            closedSubscription=g_dbus_connection_signal_subscribe(session,"org.freedesktop.Notifications","org.freedesktop.Notifications","NotificationClosed","/org/freedesktop/Notifications",nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
                +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* value,gpointer data){guint32 id{},reason{};g_variant_get(value,"(uu)",&id,&reason);auto* self=static_cast<Impl*>(data);if(self->notificationId==id)self->notificationId=0;},this,nullptr);
        }
        monitor=std::make_unique<mc::linuxplatform::OsdSystemMonitor>([this](mc::SystemEvent event){present(osd::system(event,"Linux"));},
            [](std::string line){g_print("%s\n",line.c_str());});
        g_print("OSD LINUX: backend=%s; card=%dx%d; latency=first-paint; keyboard focus unchanged; EC-reads=0 EC-writes=0\n",layerSupported?"Wayland layer-shell":"GTK overlay / desktop notifications",osd::kWidth,osd::kHeight);
    }
    void stop(){
        if(stopped)return;
        stopped=true;life->owner=nullptr;monitor.reset();
        if(timer){g_source_remove(timer);timer=0;}
        closeNotification();g_cancellable_cancel(cancel);
        if(watch){g_bus_unwatch_name(watch);watch=0;}
        if(session&&closedSubscription)g_dbus_connection_signal_unsubscribe(session,closedSubscription);
        if(layer){gtk_window_destroy(layer);layer=nullptr;}
        else if(card&&gtk_widget_get_parent(card->widget()))gtk_overlay_remove_overlay(overlay,card->widget());
        if(card){g_object_unref(card->widget());card=nullptr;}
        g_clear_object(&session);
    }
    void closeNotification(){
        if(session&&notificationId)g_dbus_connection_call(session,"org.freedesktop.Notifications","/org/freedesktop/Notifications","org.freedesktop.Notifications","CloseNotification",g_variant_new("(u)",notificationId),nullptr,G_DBUS_CALL_FLAGS_NO_AUTO_START,2000,nullptr,nullptr,nullptr);
        notificationId=0;pendingNotice.reset();
    }
    void notify(osd::Message message){
        pendingNotice=std::move(message);if(notifyBusy||!session||!serverAvailable)return;
        auto next=std::move(*pendingNotice);pendingNotice.reset();notifyBusy=true;
        lastNotifyAt=osd::now();
        GVariantBuilder hints;g_variant_builder_init(&hints,G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&hints,"{sv}","transient",g_variant_new_boolean(TRUE));
        g_variant_builder_add(&hints,"{sv}","suppress-sound",g_variant_new_boolean(TRUE));
        g_variant_builder_add(&hints,"{sv}","x-canonical-private-synchronous",g_variant_new_string("MagicControl"));
        if(next.percent)g_variant_builder_add(&hints,"{sv}","value",g_variant_new_int32(*next.percent));
        // Event payloads stay in their source language. Translate only the
        // presentation sent to the desktop, including a queued replacement.
        const auto title=ui::translate(next.title),detail=ui::translate(next.detail);
        auto* token=new std::shared_ptr<Life>(life);
        g_dbus_connection_call(session,"org.freedesktop.Notifications","/org/freedesktop/Notifications","org.freedesktop.Notifications","Notify",
            g_variant_new("(susss@as@a{sv}i)","MagicControl",notificationId,iconName(next.icon),title.c_str(),detail.c_str(),
                g_variant_new_strv(nullptr,0),g_variant_builder_end(&hints),next.pending?5000:osd::kHoldMs),G_VARIANT_TYPE("(u)"),G_DBUS_CALL_FLAGS_NO_AUTO_START,2500,cancel,
            +[](GObject* object,GAsyncResult* result,gpointer data){
                std::unique_ptr<std::shared_ptr<Life>> token(static_cast<std::shared_ptr<Life>*>(data));
                GError* error{};GVariant* value=g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),result,&error);
                if(auto* self=(*token)->owner){
                    self->notifyBusy=false;
                    if(value)g_variant_get(value,"(u)",&self->notificationId);
                    if(!self->enabled||!self->usingDesktop)self->closeNotification();
                    else if(self->pendingNotice){auto latest=std::move(*self->pendingNotice);self->pendingNotice.reset();self->notify(std::move(latest));}
                    if(error)g_print("OSD notification: %s\n",error->message);
                }
                if(value)g_variant_unref(value);
                if(error)g_error_free(error);
            },token);
    }
    void present(osd::Message message){
        if(!enabled||stopped)return;
        if(!message.receivedAt&&!message.issuedAt)message.issuedAt=osd::now();
        state.present(std::move(message),osd::now());render(true);
    }
    void render(bool changed=false){
        if(stopped)return;
        const auto at=osd::now();state.tick(at);
        if(!state.visible){
            usingDesktop=false;
            if(notificationId||pendingNotice)closeNotification();
            gtk_widget_set_visible(card->widget(),FALSE);if(layer)gtk_widget_set_visible(GTK_WIDGET(layer),FALSE);
            if(timer){g_source_remove(timer);timer=0;}return;
        }
        const auto& message=state.message;
        const bool desktop=serverAvailable&&!layerSupported&&!gtk_window_is_active(owner);
        usingDesktop=desktop;
        if(desktop){
            gtk_widget_set_visible(card->widget(),FALSE);
            if(changed||lastRendered!=state.revision||(message.pending&&at-lastNotifyAt>=4000))notify(message);
        } else {
            if(notificationId)closeNotification();
#ifdef MAGICCONTROL_LAYER_SHELL
            if(layer){
                GdkSurface* surface=gtk_native_get_surface(GTK_NATIVE(owner));
                GdkMonitor* screen=surface?gdk_display_get_monitor_at_surface(gtk_widget_get_display(GTK_WIDGET(owner)),surface):nullptr;
                if(screen){GdkRectangle bounds{};gdk_monitor_get_geometry(screen,&bounds);gtk_layer_set_monitor(layer,screen);
                    gtk_layer_set_margin(layer,GTK_LAYER_SHELL_EDGE_LEFT,osd::kLeft);
                    gtk_layer_set_margin(layer,GTK_LAYER_SHELL_EDGE_TOP,std::max(0,static_cast<int>(bounds.height*.37)-osd::kHeight/2));}
                gtk_widget_set_visible(GTK_WIDGET(layer),TRUE);
            } else
#endif
            {
                gtk_widget_set_margin_start(card->widget(),osd::kLeft);
                gtk_widget_set_margin_top(card->widget(),std::max(0,static_cast<int>(gtk_widget_get_height(GTK_WIDGET(overlay))*.37)-osd::kHeight/2));
            }
            gtk_widget_set_opacity(card->widget(),state.opacity(at));gtk_widget_set_visible(card->widget(),TRUE);
            if(changed||lastRendered!=state.revision||state.animating(at))card->redraw();
        }
        lastRendered=state.revision;
        if(!timer)timer=g_timeout_add(15,+[](gpointer data)->gboolean{auto* self=static_cast<Impl*>(data);self->render();return self->timer?G_SOURCE_CONTINUE:G_SOURCE_REMOVE;},this);
    }
    void paint(Canvas& c,cairo_t* cr,int width,int height){
        const ui::Scale s{c.dpi()};const auto at=osd::now();const auto& message=state.message;
        const Color accent=message.error?osd::kError:osd::kAccent;
        color(cr,osd::kBackground);rounded(cr,width,height,s(osd::kRadius));cairo_fill(cr);
        cairo_save(cr);cairo_translate(cr,1,1);color(cr,osd::kBorder);cairo_set_line_width(cr,std::max(1,s(1)));
        rounded(cr,width-3,height-3,s(osd::kRadius));cairo_stroke(cr);cairo_restore(cr);
        const int pad=s(osd::kPadding),left=pad+s(osd::kIconWidth+osd::kIconGap),right=width-pad;
        cairo_save(cr);color(cr,accent);const double side=s(35);
        cairo_translate(cr,pad+(s(osd::kIconWidth)-side)/2,pad+(s(osd::kIconHeight)-side)/2);
        cairo_scale(cr,side/32,side/32);cairo_set_line_width(cr,1.2);
        osd::drawIcon(message.icon,[&](int a,int b,int x,int y){cairo_move_to(cr,a,b);cairo_line_to(cr,x,y);},
            [&](int a,int b,int x,int y){cairo_rectangle(cr,a,b,x-a,y-b);},
            [&](int a,int b,int x,int y){cairo_save(cr);cairo_translate(cr,(a+x)/2.,(b+y)/2.);cairo_scale(cr,(x-a)/2.,(y-b)/2.);cairo_new_sub_path(cr);cairo_arc(cr,0,0,1,0,6.28318530718);cairo_restore(cr);});
        cairo_stroke(cr);cairo_restore(cr);
        text(cr,message.title,{left,pad,right,pad+s(osd::kTitleHeight)},Font::OsdTitle,osd::kText,c.dpi(),Horizontal::Left,Vertical::Top,true);
        text(cr,message.detail,{left,pad+s(osd::kDetailTop),right,pad+s(osd::kDetailTop+osd::kDetailHeight)},Font::OsdDetail,osd::kDetail,c.dpi(),Horizontal::Left,Vertical::Top,true);
        if(message.percent){
            Rect track{pad,height-s(osd::kBarBottom),width-pad,height-s(osd::kBarBottom)+std::max(3,s(osd::kBarHeight))};fill(cr,track,osd::kTrack);
            const int x=track.left+static_cast<int>(track.width()*std::clamp(state.percent(at),0.0,100.0)/100.0);
            fill(cr,{track.left,track.top,x,track.bottom},accent);
            if(message.key=="keyboard"){
                const double y=(track.top+track.bottom)/2.0;
                if(state.activity(at)>0){cairo_set_source_rgba(cr,(accent&255)/255.0,((accent>>8)&255)/255.0,((accent>>16)&255)/255.0,.25*state.activity(at));cairo_arc(cr,x,y,s(7),0,6.283185307);cairo_fill(cr);}
                color(cr,accent);cairo_arc(cr,x,y,s(3),0,6.283185307);cairo_fill(cr);
            }
        }
        if(lastPainted!=state.revision&&message.receivedAt&&at-lastLogged>=1000){
            lastLogged=at;g_print("OSD DELIVERY: key=%s event-to-paint-ms=%llu backend=GTK EC-reads=0 EC-writes=0\n",
                message.key.c_str(),static_cast<unsigned long long>(osd::now()-message.receivedAt));
        }
        lastPainted=state.revision;
    }
};
OsdService::OsdService(GtkWindow* owner,GtkOverlay* overlay):impl_(std::make_unique<Impl>()){impl_->initialize(owner,overlay);}
OsdService::~OsdService()=default;
void OsdService::show(osd::Message message){impl_->present(std::move(message));}
void OsdService::hide(){impl_->state.dismiss();impl_->render(true);}
std::uint64_t OsdService::beginOperation(osd::Message message){if(!impl_->enabled||impl_->stopped)return 0;if(!message.receivedAt&&!message.issuedAt)message.issuedAt=osd::now();const auto token=++impl_->nextToken;impl_->state.begin(token,std::move(message),osd::now());impl_->render(true);return token;}
void OsdService::completeOperation(std::uint64_t token,osd::Message message){if(impl_->enabled&&!impl_->stopped&&token){impl_->state.complete(token,std::move(message),osd::now());impl_->render(true);}}
void OsdService::setEnabled(bool enabled){impl_->enabled=enabled;if(!enabled){impl_->state.dismiss();impl_->closeNotification();impl_->render();}}
void OsdService::refreshLanguage(){impl_->render(true);}
void OsdService::stop(){impl_->stop();}
#ifdef MAGICCONTROL_GTK_SETTINGS_TEST
std::optional<mc::gui::osd::Message> OsdService::testMessage() const{
    if(!impl_->state.visible)return {};return impl_->state.message;
}
#endif
}
