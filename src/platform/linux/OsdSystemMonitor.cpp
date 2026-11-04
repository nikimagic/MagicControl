#include "OsdSystemMonitor.h"
#include <gio/gio.h>
#include <pulse/pulseaudio.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mc::linuxplatform {
namespace {
std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
constexpr const char* kdeService="org.kde.Solid.PowerManagement";
constexpr const char* kdePath="/org/kde/Solid/PowerManagement/Actions/BrightnessControl";
constexpr const char* kdeInterface="org.kde.Solid.PowerManagement.Actions.BrightnessControl";
}
struct OsdSystemMonitor::Impl {
    Callback callback;std::function<void(std::string)> log;
    struct Life{Impl* owner;};
    std::shared_ptr<Life> life=std::make_shared<Life>(Life{this});
    struct Task{std::shared_ptr<Life> life;std::function<void(Impl&)> function;};
    pa_threaded_mainloop* loop{};pa_context* context{};
    GDBusConnection* session{};GDBusConnection* system{};
    GCancellable* cancel{g_cancellable_new()};
    std::vector<std::pair<GDBusConnection*,guint>> signals;
    guint retry{};
    bool stopped{},sinkSeen{},sourceSeen{},loopStarted{};
    std::string sink,source;
    int sinkPercent{},sourcePercent{};bool sinkMute{},sourceMute{};
    int keyboardMaximum{},kdeMaximum{};
    std::optional<int> lastKdeValue;
    std::optional<SystemEvent> lastEvent;

    ~Impl(){
        stopped=true;life->owner=nullptr;
        if(retry)g_source_remove(retry);
        stopAudio();g_cancellable_cancel(cancel);
        for(auto [bus,id]:signals)g_dbus_connection_signal_unsubscribe(bus,id);
        g_clear_object(&session);g_clear_object(&system);g_object_unref(cancel);
    }
    void post(std::function<void(Impl&)> function){
        auto* task=new Task{life,std::move(function)};
        g_main_context_invoke_full(nullptr,G_PRIORITY_DEFAULT,+[](gpointer data)->gboolean{
            auto& task=*static_cast<Task*>(data);if(task.life->owner)task.function(*task.life->owner);return G_SOURCE_REMOVE;
        },task,+[](gpointer data){delete static_cast<Task*>(data);});
    }
    void emit(SystemEvent event){
        if(stopped)return;
        if(lastEvent&&lastEvent->kind==event.kind&&lastEvent->value==event.value&&lastEvent->muted==event.muted&&event.receivedAt-lastEvent->receivedAt<25)return;
        lastEvent=event;if(callback)callback(event);
    }
    void stopAudio(){
        if(loop&&loopStarted)pa_threaded_mainloop_stop(loop);
        loopStarted=false;
        if(context){pa_context_set_state_callback(context,nullptr,nullptr);pa_context_set_subscribe_callback(context,nullptr,nullptr);pa_context_disconnect(context);pa_context_unref(context);context=nullptr;}
        if(loop){pa_threaded_mainloop_free(loop);loop=nullptr;}
    }
    void startAudio(){
        stopAudio();sinkSeen=sourceSeen=false;sink.clear();source.clear();
        loop=pa_threaded_mainloop_new();if(!loop)return;
        context=pa_context_new(pa_threaded_mainloop_get_api(loop),"MagicControl OSD");
        if(!context)return;
        pa_context_set_state_callback(context,+[](pa_context* context,void* data){
            auto* self=static_cast<Impl*>(data);
            if(pa_context_get_state(context)==PA_CONTEXT_READY){
                pa_context_set_subscribe_callback(context,subscribed,self);
                if(auto* operation=pa_context_subscribe(context,static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK|PA_SUBSCRIPTION_MASK_SOURCE|PA_SUBSCRIPTION_MASK_SERVER),nullptr,nullptr))pa_operation_unref(operation);
                self->server();self->post([](Impl& owner){if(owner.log)owner.log("OSD AUDIO: PulseAudio/PipeWire notifications connected; no audio recording");});
            } else if(pa_context_get_state(context)==PA_CONTEXT_FAILED||pa_context_get_state(context)==PA_CONTEXT_TERMINATED)
                self->post([](Impl& owner){owner.scheduleReconnect();});
        },this);
        if(pa_context_connect(context,nullptr,PA_CONTEXT_NOFLAGS,nullptr)<0){scheduleReconnect();return;}
        loopStarted=pa_threaded_mainloop_start(loop)>=0;
        if(!loopStarted)scheduleReconnect();
    }
    void scheduleReconnect(){
        if(stopped||retry)return;
        if(log)log("OSD AUDIO: server unavailable; reconnect scheduled");
        retry=g_timeout_add_seconds(5,+[](gpointer data)->gboolean{
            auto* self=static_cast<Impl*>(data);self->retry=0;self->startAudio();return G_SOURCE_REMOVE;
        },this);
    }
    void server(){
        if(auto* operation=pa_context_get_server_info(context,+[](pa_context*,const pa_server_info* info,void* data){
            if(!info)return;
            auto* self=static_cast<Impl*>(data);
            const std::string sink=info->default_sink_name?info->default_sink_name:"",source=info->default_source_name?info->default_source_name:"";
            if(self->sink!=sink){self->sink=sink;self->sinkSeen=false;}
            if(self->source!=source){self->source=source;self->sourceSeen=false;}
            self->output();self->input();
        },this))pa_operation_unref(operation);
    }
    void output(){
        if(sink.empty())return;
        if(auto* operation=pa_context_get_sink_info_by_name(context,sink.c_str(),+[](pa_context*,const pa_sink_info* info,int end,void* data){
            if(end||!info)return;
            auto* self=static_cast<Impl*>(data);
            if(!info->name||self->sink!=info->name)return;
            const int percent=std::clamp(static_cast<int>(std::lround(pa_cvolume_avg(&info->volume)*100.0/PA_VOLUME_NORM)),0,150);
            const bool mute=info->mute!=0;
            if(self->sinkSeen&&(percent!=self->sinkPercent||mute!=self->sinkMute)){
                const SystemEvent event{SystemEventKind::Volume,percent,mute,now()};self->post([event](Impl& owner){owner.emit(event);});
            }
            self->sinkSeen=true;self->sinkPercent=percent;self->sinkMute=mute;
        },this))pa_operation_unref(operation);
    }
    void input(){
        if(source.empty())return;
        if(auto* operation=pa_context_get_source_info_by_name(context,source.c_str(),+[](pa_context*,const pa_source_info* info,int end,void* data){
            if(end||!info)return;
            auto* self=static_cast<Impl*>(data);
            if(!info->name||self->source!=info->name)return;
            const bool mute=info->mute!=0;
            if(self->sourceSeen&&mute!=self->sourceMute){
                const SystemEvent event{SystemEventKind::Microphone,0,mute,now()};self->post([event](Impl& owner){owner.emit(event);});
            }
            self->sourceSeen=true;self->sourceMute=mute;
        },this))pa_operation_unref(operation);
    }
    static void subscribed(pa_context*,pa_subscription_event_type_t event,std::uint32_t,void* data){
        auto* self=static_cast<Impl*>(data);const auto facility=event&PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        if(facility==PA_SUBSCRIPTION_EVENT_SERVER)self->server();
        else if(facility==PA_SUBSCRIPTION_EVENT_SINK)self->output();
        else if(facility==PA_SUBSCRIPTION_EVENT_SOURCE)self->input();
    }
    void subscribe(GDBusConnection* bus,const char* sender,const char* interface,const char* signal,const char* path,GDBusSignalCallback handler){
        if(bus)signals.emplace_back(bus,g_dbus_connection_signal_subscribe(bus,sender,interface,signal,path,nullptr,G_DBUS_SIGNAL_FLAGS_NONE,handler,this,nullptr));
    }
    void readMaximum(bool keyboard){
        GDBusConnection* bus=keyboard?system:session;if(!bus)return;
        auto* token=new std::pair<std::shared_ptr<Life>,bool>{life,keyboard};
        g_dbus_connection_call(bus,keyboard?"org.freedesktop.UPower":kdeService,
            keyboard?"/org/freedesktop/UPower/KbdBacklight":kdePath,
            keyboard?"org.freedesktop.UPower.KbdBacklight":kdeInterface,
            keyboard?"GetMaxBrightness":"brightnessMax",nullptr,G_VARIANT_TYPE("(i)"),G_DBUS_CALL_FLAGS_NO_AUTO_START,2000,cancel,
            +[](GObject* object,GAsyncResult* result,gpointer data){
                std::unique_ptr<std::pair<std::shared_ptr<Life>,bool>> token(static_cast<std::pair<std::shared_ptr<Life>,bool>*>(data));
                GError* error{};GVariant* value=g_dbus_connection_call_finish(G_DBUS_CONNECTION(object),result,&error);
                if(value){int maximum{};g_variant_get(value,"(i)",&maximum);
                    if(auto* self=token->first->owner){
                        if(token->second)self->keyboardMaximum=std::max(0,maximum);
                        else {self->kdeMaximum=std::max(0,maximum);if(self->lastKdeValue)self->kdeBrightness(*self->lastKdeValue);}
                    }
                    g_variant_unref(value);
                }
                if(error)g_error_free(error);
            },token);
    }
    void kdeBrightness(int value){
        lastKdeValue=value;if(kdeMaximum>0)emit({SystemEventKind::Brightness,std::clamp(value*100/kdeMaximum,0,100),false,now()});
    }
    void connectDesktop(){
        GError* error{};session=g_bus_get_sync(G_BUS_TYPE_SESSION,cancel,&error);if(error){g_error_free(error);error=nullptr;}
        system=g_bus_get_sync(G_BUS_TYPE_SYSTEM,cancel,&error);if(error)g_error_free(error);
        subscribe(session,"org.gnome.SettingsDaemon.Power","org.freedesktop.DBus.Properties","PropertiesChanged","/org/gnome/SettingsDaemon/Power",
            +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* value,gpointer data){
                auto* self=static_cast<Impl*>(data);const gchar* interface{};GVariant *changed{},*invalidated{};
                g_variant_get(value,"(&s@a{sv}@as)",&interface,&changed,&invalidated);
                int percent{};if(g_strcmp0(interface,"org.gnome.SettingsDaemon.Power.Screen")==0&&g_variant_lookup(changed,"Brightness","i",&percent)&&percent>=0&&percent<=100)
                    self->emit({SystemEventKind::Brightness,percent,false,now()});
                g_variant_unref(changed);g_variant_unref(invalidated);
            });
        subscribe(session,kdeService,kdeInterface,"brightnessChanged",kdePath,
            +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* value,gpointer data){int level{};g_variant_get(value,"(i)",&level);static_cast<Impl*>(data)->kdeBrightness(level);});
        subscribe(session,kdeService,kdeInterface,"brightnessMaxChanged",kdePath,
            +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* value,gpointer data){g_variant_get(value,"(i)",&static_cast<Impl*>(data)->kdeMaximum);});
        subscribe(system,"org.freedesktop.UPower","org.freedesktop.UPower.KbdBacklight","BrightnessChanged","/org/freedesktop/UPower/KbdBacklight",
            +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* value,gpointer data){
                auto* self=static_cast<Impl*>(data);int level{};g_variant_get(value,"(i)",&level);
                if(self->keyboardMaximum>0)self->emit({SystemEventKind::KeyboardBacklight,level,false,now(),self->keyboardMaximum});
                else self->readMaximum(true);
            });
        subscribe(system,"org.freedesktop.login1","org.freedesktop.login1.Manager","PrepareForSleep","/org/freedesktop/login1",
            +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* value,gpointer data){
                gboolean asleep{};g_variant_get(value,"(b)",&asleep);if(!asleep){auto* self=static_cast<Impl*>(data);self->startAudio();self->readMaximum(true);self->readMaximum(false);}
            });
        readMaximum(true);readMaximum(false);
        if(log)log("OSD DESKTOP: read-only GNOME/KDE brightness and UPower keyboard subscriptions installed; unsupported sources stay inactive");
    }
};
OsdSystemMonitor::OsdSystemMonitor(Callback callback,std::function<void(std::string)> log):impl_(std::make_unique<Impl>()){
    impl_->callback=std::move(callback);impl_->log=std::move(log);impl_->connectDesktop();impl_->startAudio();
}
OsdSystemMonitor::~OsdSystemMonitor()=default;
}
