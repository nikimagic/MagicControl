#include "DisplayHotkeyLinux.h"
#include <gio/gio.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
namespace mc::linux_platform {
namespace {
constexpr const char* service="org.freedesktop.portal.Desktop";
constexpr const char* object="/org/freedesktop/portal/desktop";
constexpr const char* interface="org.freedesktop.portal.GlobalShortcuts";
constexpr const char* action="sc-rotate";
std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
}
struct DisplayRotationHotkey::Impl {
    std::function<void(std::uint64_t)> activated;std::function<void(std::string)> log;
    GDBusConnection* bus{};GCancellable* cancel{g_cancellable_new()};guint watch{},response{},key{},released{},closed{};
    std::string session,pending;bool binding{},pressed{},stopped{};unsigned serial{};
    std::string prefix=[] {gchar* id=g_uuid_string_random();std::string text=id;g_free(id);std::replace(text.begin(),text.end(),'-','_');return "magiccontrol_sc_"+text+"_";}();
    struct Life {Impl* owner;};std::shared_ptr<Life> life{std::make_shared<Life>(Life{this})};
    static Impl* owner(gpointer p){return (*static_cast<std::shared_ptr<Life>*>(p))->owner;}
    gpointer hold(){return new std::shared_ptr<Life>(life);}
    static void drop(gpointer p){delete static_cast<std::shared_ptr<Life>*>(p);}
    void report(std::string message){if(log)log("Sc global shortcut: "+message);}
    void close(const std::string& path,const char* iface){if(bus&&!path.empty())g_dbus_connection_call(bus,service,path.c_str(),iface,"Close",nullptr,nullptr,G_DBUS_CALL_FLAGS_NO_AUTO_START,2000,nullptr,nullptr,nullptr);}
    ~Impl(){stopped=true;life->owner=nullptr;g_cancellable_cancel(cancel);
        if(bus){close(pending,"org.freedesktop.portal.Request");close(session,"org.freedesktop.portal.Session");
            for(const auto id:{response,key,released,closed})if(id)g_dbus_connection_signal_unsubscribe(bus,id);}
        if(watch)g_bus_unwatch_name(watch);g_clear_object(&bus);g_object_unref(cancel);}
    void call(const char* method,GVariant* parameters){
        g_dbus_connection_call(bus,service,object,interface,method,parameters,G_VARIANT_TYPE("(o)"),G_DBUS_CALL_FLAGS_NONE,10000,cancel,
            +[](GObject* bus,GAsyncResult* result,gpointer p){auto* self=owner(p);GError* error{};auto* reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(bus),result,&error);
                if(self&&!self->stopped){if(error){self->pending.clear();self->report(error->message);}else{const char* path{};g_variant_get(reply,"(&o)",&path);
                    // A fast Response may already have advanced the session. Do
                    // not overwrite its next request or the accepted binding.
                    if(!path||!g_variant_is_object_path(path))self->report("invalid portal response path");}}
                if(reply)g_variant_unref(reply);if(error)g_error_free(error);drop(p);},hold());
    }
    std::string token(){return prefix+std::to_string(++serial);}
    void expect(const std::string& token){std::string sender=g_dbus_connection_get_unique_name(bus);if(!sender.empty()&&sender[0]==':')sender.erase(0,1);std::replace(sender.begin(),sender.end(),'.','_');pending="/org/freedesktop/portal/desktop/request/"+sender+"/"+token;}
    void create(){
        if(stopped||!pending.empty()||!session.empty())return;
        const auto request=token(),sessionToken=token();expect(request);GVariantBuilder options;g_variant_builder_init(&options,G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options,"{sv}","handle_token",g_variant_new_string(request.c_str()));g_variant_builder_add(&options,"{sv}","session_handle_token",g_variant_new_string(sessionToken.c_str()));
        report("requesting a compositor binding for Sc/XF86RotateWindows; the desktop may ask permission");call("CreateSession",g_variant_new("(a{sv})",&options));
    }
    void bind(){
        const auto request=token();expect(request);
        GVariantBuilder options,properties,shortcuts;g_variant_builder_init(&options,G_VARIANT_TYPE_VARDICT);g_variant_builder_add(&options,"{sv}","handle_token",g_variant_new_string(request.c_str()));
        g_variant_builder_init(&properties,G_VARIANT_TYPE_VARDICT);g_variant_builder_add(&properties,"{sv}","description",g_variant_new_string("Sc: поворот встроенного экрана на180°"));g_variant_builder_add(&properties,"{sv}","preferred_trigger",g_variant_new_string("XF86RotateWindows"));
        g_variant_builder_init(&shortcuts,G_VARIANT_TYPE("a(sa{sv})"));g_variant_builder_add(&shortcuts,"(sa{sv})",action,&properties);
        call("BindShortcuts",g_variant_new("(oa(sa{sv})sa{sv})",session.c_str(),&shortcuts,"",&options));
    }
    void result(const char* path,GVariant* value){
        if(pending!=path)return;pending.clear();guint32 code{};GVariant* results{};g_variant_get(value,"(u@a{sv})",&code,&results);
        if(code){report(code==1?"binding cancelled by user":"binding denied/unavailable");g_variant_unref(results);return;}
        if(session.empty()){const char* handle{};if(g_variant_lookup(results,"session_handle","&s",&handle)&&g_variant_is_object_path(handle)){session=handle;bind();}else report("session identity unavailable");}
        else{auto* shortcuts=g_variant_lookup_value(results,"shortcuts",G_VARIANT_TYPE("a(sa{sv})"));if(shortcuts){GVariantIter entries;g_variant_iter_init(&entries,shortcuts);const char* id{};GVariant* properties{};
                while(g_variant_iter_next(&entries,"(&s@a{sv})",&id,&properties)){if(g_strcmp0(id,action)==0){binding=true;const char* trigger{};if(g_variant_lookup(properties,"trigger_description","&s",&trigger))report(std::string("desktop trigger: ")+trigger);}g_variant_unref(properties);}g_variant_unref(shortcuts);}
            report(binding?"Sc action bound globally; active even while MagicControl is hidden":"desktop returned no Sc binding");}
        g_variant_unref(results);
    }
    void initialize(){GError* error{};bus=g_bus_get_sync(G_BUS_TYPE_SESSION,cancel,&error);if(error){report(error->message);g_error_free(error);}if(!bus)return;
        response=g_dbus_connection_signal_subscribe(bus,service,"org.freedesktop.portal.Request","Response",nullptr,nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
            +[](GDBusConnection*,const gchar*,const gchar* path,const gchar*,const gchar*,GVariant* value,gpointer p){if(auto* self=owner(p))self->result(path,value);},hold(),drop);
        key=g_dbus_connection_signal_subscribe(bus,service,interface,"Activated",object,nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
            +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* value,gpointer p){if(auto* self=owner(p)){const char *session{},*id{};guint64 timestamp{};GVariant* options{};g_variant_get(value,"(&o&st@a{sv})",&session,&id,&timestamp,&options);g_variant_unref(options);
                if(self->binding&&self->session==session&&g_strcmp0(id,action)==0&&!self->pressed){self->pressed=true;if(self->activated)self->activated(now());}}},hold(),drop);
        released=g_dbus_connection_signal_subscribe(bus,service,interface,"Deactivated",object,nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
            +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant* value,gpointer p){if(auto* self=owner(p)){const char *session{},*id{};guint64 timestamp{};GVariant* options{};g_variant_get(value,"(&o&st@a{sv})",&session,&id,&timestamp,&options);g_variant_unref(options);if(self->session==session&&g_strcmp0(id,action)==0)self->pressed=false;}},hold(),drop);
        closed=g_dbus_connection_signal_subscribe(bus,service,"org.freedesktop.portal.Session","Closed",nullptr,nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
            +[](GDBusConnection*,const gchar*,const gchar* path,const gchar*,const gchar*,GVariant*,gpointer p){if(auto* self=owner(p))if(self->session==path){self->session.clear();self->binding=false;self->pressed=false;self->report("session closed");}},hold(),drop);
        watch=g_bus_watch_name_on_connection(bus,service,G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
            +[](GDBusConnection*,const gchar*,const gchar*,gpointer p){if(auto* self=owner(p))self->create();},
            +[](GDBusConnection*,const gchar*,gpointer p){if(auto* self=owner(p)){self->binding=false;self->pressed=false;self->session.clear();self->pending.clear();}},hold(),drop);
    }
};
DisplayRotationHotkey::DisplayRotationHotkey(std::function<void(std::uint64_t)> activated,std::function<void(std::string)> log):impl_(std::make_unique<Impl>()){
    impl_->activated=std::move(activated);impl_->log=std::move(log);impl_->initialize();}
DisplayRotationHotkey::~DisplayRotationHotkey()=default;
bool DisplayRotationHotkey::bound()const noexcept{return impl_->binding;}
}
