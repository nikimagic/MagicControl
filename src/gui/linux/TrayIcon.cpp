#include "TrayIcon.h"
#include "Ui.h"
#include "../../platform/linux/SettingsServicesLinux.h"
#include <gtk/gtk.h>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace mc::gtkui {
namespace {
constexpr const char* watcher = "org.kde.StatusNotifierWatcher";
constexpr const char* notifier = "org.kde.StatusNotifierItem";
constexpr const char* menuInterface = "com.canonical.dbusmenu";
constexpr const char* introspection = R"xml(
<node>
 <interface name="org.kde.StatusNotifierItem">
  <method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
  <method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
  <method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
  <method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method>
  <property name="Category" type="s" access="read"/><property name="Id" type="s" access="read"/>
  <property name="Title" type="s" access="read"/><property name="Status" type="s" access="read"/>
  <property name="WindowId" type="u" access="read"/><property name="IconName" type="s" access="read"/>
  <property name="IconPixmap" type="a(iiay)" access="read"/><property name="IconThemePath" type="s" access="read"/>
  <property name="OverlayIconName" type="s" access="read"/><property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
  <property name="AttentionIconName" type="s" access="read"/><property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
  <property name="AttentionMovieName" type="s" access="read"/><property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
  <property name="ItemIsMenu" type="b" access="read"/><property name="Menu" type="o" access="read"/>
  <signal name="NewIcon"/><signal name="NewTitle"/><signal name="NewToolTip"/><signal name="NewStatus"><arg type="s"/></signal>
 </interface>
 <interface name="com.canonical.dbusmenu">
  <method name="GetLayout"><arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/>
   <arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/></method>
  <method name="GetGroupProperties"><arg type="ai" direction="in"/><arg type="as" direction="in"/><arg type="a(ia{sv})" direction="out"/></method>
  <method name="GetProperty"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
  <method name="Event"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="u" direction="in"/></method>
  <method name="EventGroup"><arg type="a(isvu)" direction="in"/><arg type="ai" direction="out"/></method>
  <method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
  <method name="AboutToShowGroup"><arg type="ai" direction="in"/><arg type="ai" direction="out"/><arg type="ai" direction="out"/></method>
  <property name="Version" type="u" access="read"/><property name="TextDirection" type="s" access="read"/>
  <property name="Status" type="s" access="read"/><property name="IconThemePath" type="as" access="read"/>
  <signal name="LayoutUpdated"><arg type="u"/><arg type="i"/></signal>
  <signal name="ItemsPropertiesUpdated"><arg type="a(ia{sv})"/><arg type="a(ias)"/></signal>
 </interface>
</node>)xml";
struct Entry { const char* label; bool enabled; std::vector<int> children; bool separator{}; };
const std::array<Entry,22> entries{{
    {"",true,{1,2,6,10,17}}, {"Открыть",true,{}}, {"Батарея",true,{3,4,5,21}},
    {"100%",false,{}}, {"95%",false,{}}, {"85%",false,{}},
    {"Мощность",true,{18,7,8,9,19,20}}, {"Офисный",false,{}}, {"Сбалансированный",false,{}}, {"Турбо",false,{}},
    {"Подсветка",true,{11,12,13,14,15,16}}, {"Уровень 1",false,{}}, {"Уровень 2",false,{}},
    {"Уровень 3",false,{}}, {"Уровень 4",false,{}}, {"",false,{},true}, {"Выкл.",false,{}}, {"Выйти",true,{}},
    {"Экономичный",false,{}}, {"",false,{},true}, {"Limiter EC",false,{}}, {"60%",false,{}}
}};
const char* ecPolicyLabel() {
    return linux_platform::hasAnyOnlinePowerSupply()?"Turbo EC":"Limiter EC";
}
GVariant* properties(int id) {
    GVariantBuilder result; g_variant_builder_init(&result,G_VARIANT_TYPE_VARDICT);
    if (id >= 0 && id < static_cast<int>(entries.size())) {
        const auto& entry=entries[id];
        if(entry.separator)g_variant_builder_add(&result,"{sv}","type",g_variant_new_string("separator"));
        else {
            const auto caption=ui::translate(id==20?ecPolicyLabel():entry.label);
            g_variant_builder_add(&result,"{sv}","label",g_variant_new_string(caption.c_str()));
            g_variant_builder_add(&result,"{sv}","enabled",g_variant_new_boolean(entry.enabled));
            g_variant_builder_add(&result,"{sv}","visible",g_variant_new_boolean(TRUE));
            if(!entry.children.empty())g_variant_builder_add(&result,"{sv}","children-display",g_variant_new_string("submenu"));
        }
    }
    return g_variant_builder_end(&result);
}
GVariant* layout(int id,int depth) {
    GVariantBuilder children;g_variant_builder_init(&children,G_VARIANT_TYPE("av"));
    if(id>=0&&id<static_cast<int>(entries.size())&&depth!=0)
        for(int child:entries[id].children)g_variant_builder_add(&children,"v",layout(child,depth<0?-1:depth-1));
    return g_variant_new("(i@a{sv}@av)",id,properties(id),g_variant_builder_end(&children));
}
}

struct TrayIcon::Impl {
    struct Life { Impl* owner; };
    std::shared_ptr<Life> life{std::make_shared<Life>(Life{this})};
    std::function<void()> open,exit;
    std::function<void(bool)> availabilityChanged;
    GDBusConnection* connection{};
    GDBusNodeInfo* node{};
    GdkPixbuf* icon{};
    guint itemId{},menuId{},watchId{},hostSignal{},revision{1};
    bool ready{};
    Impl(std::function<void()> show,std::function<void()> quit,std::function<void(bool)> availability)
        :open(std::move(show)),exit(std::move(quit)),availabilityChanged(std::move(availability)) {
        GError* error{};
        connection=g_bus_get_sync(G_BUS_TYPE_SESSION,nullptr,&error);
        if(!connection){g_warning("Tray session bus unavailable: %s",error?error->message:"");g_clear_error(&error);return;}
        node=g_dbus_node_info_new_for_xml(introspection,&error);
        if(!node){g_warning("Tray interface: %s",error?error->message:"");g_clear_error(&error);return;}
        icon=gdk_pixbuf_new_from_resource("/com/mechrevo/magiccontrol/icons/512x512/apps/com.mechrevo.magiccontrol.png",&error);
        if(error){g_warning("Tray icon: %s",error->message);g_clear_error(&error);}
        static const GDBusInterfaceVTable table{method,property,nullptr,{nullptr}};
        itemId=g_dbus_connection_register_object(connection,"/StatusNotifierItem",node->interfaces[0],&table,this,nullptr,&error);
        if(error){g_warning("Tray item: %s",error->message);g_clear_error(&error);}
        menuId=g_dbus_connection_register_object(connection,"/MenuBar",node->interfaces[1],&table,this,nullptr,&error);
        if(error){g_warning("Tray menu: %s",error->message);g_clear_error(&error);}
        if(!itemId||!menuId)return;
        watchId=g_bus_watch_name_on_connection(connection,watcher,G_BUS_NAME_WATCHER_FLAGS_NONE,
            +[](GDBusConnection*,const gchar*,const gchar*,gpointer data){static_cast<Impl*>(data)->registerItem();},
            +[](GDBusConnection*,const gchar*,gpointer data){static_cast<Impl*>(data)->setReady(false);},this,nullptr);
        hostSignal=g_dbus_connection_signal_subscribe(connection,watcher,watcher,nullptr,
            "/StatusNotifierWatcher",nullptr,G_DBUS_SIGNAL_FLAGS_NONE,
            +[](GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar* signal,GVariant*,gpointer data){
                if(g_str_has_prefix(signal,"StatusNotifierHost"))static_cast<Impl*>(data)->readHost();},this,nullptr);
    }
    ~Impl(){
        life->owner=nullptr;
        if(watchId)g_bus_unwatch_name(watchId);
        if(hostSignal)g_dbus_connection_signal_unsubscribe(connection,hostSignal);
        if(itemId)g_dbus_connection_unregister_object(connection,itemId);
        if(menuId)g_dbus_connection_unregister_object(connection,menuId);
        if(icon)g_object_unref(icon);
        if(node)g_dbus_node_info_unref(node);
        if(connection)g_object_unref(connection);
    }
    void setReady(bool value){if(ready==value)return;ready=value;g_print("Tray host: %s\n",ready?"available":"unavailable");if(availabilityChanged)availabilityChanged(ready);}
    void refreshLanguage(){
        if(!connection||!menuId)return;
        if(++revision==0)++revision;
        // Native hosts cache DBusMenu labels. Publish both revisions and changed
        // properties; the model IDs/enabled actions remain exactly the same.
        GVariantBuilder changed,removed;g_variant_builder_init(&changed,G_VARIANT_TYPE("a(ia{sv})"));
        g_variant_builder_init(&removed,G_VARIANT_TYPE("a(ias)"));
        for(int id=0;id<static_cast<int>(entries.size());++id)
            g_variant_builder_add(&changed,"(i@a{sv})",id,properties(id));
        g_dbus_connection_emit_signal(connection,nullptr,"/MenuBar",menuInterface,"ItemsPropertiesUpdated",
            g_variant_new("(@a(ia{sv})@a(ias))",g_variant_builder_end(&changed),g_variant_builder_end(&removed)),nullptr);
        g_dbus_connection_emit_signal(connection,nullptr,"/MenuBar",menuInterface,"LayoutUpdated",g_variant_new("(ui)",revision,0),nullptr);
        if(itemId)g_dbus_connection_emit_signal(connection,nullptr,"/StatusNotifierItem",notifier,"NewToolTip",nullptr,nullptr);
    }
    void registerItem(){
        g_dbus_connection_call(connection,watcher,"/StatusNotifierWatcher",watcher,"RegisterStatusNotifierItem",
            g_variant_new("(s)",g_dbus_connection_get_unique_name(connection)),nullptr,G_DBUS_CALL_FLAGS_NONE,3000,nullptr,
            +[](GObject* source,GAsyncResult* result,gpointer data){
                std::unique_ptr<std::shared_ptr<Life>> weak(static_cast<std::shared_ptr<Life>*>(data));
                GError* error{};GVariant* reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&error);
                if(reply)g_variant_unref(reply);
                if(auto* self=(*weak)->owner){if(error){g_warning("Tray registration: %s",error->message);self->setReady(false);}else self->readHost();}
                g_clear_error(&error);
            },new std::shared_ptr<Life>(life));
    }
    void readHost(){
        g_dbus_connection_call(connection,watcher,"/StatusNotifierWatcher","org.freedesktop.DBus.Properties","Get",
            g_variant_new("(ss)",watcher,"IsStatusNotifierHostRegistered"),G_VARIANT_TYPE("(v)"),G_DBUS_CALL_FLAGS_NONE,3000,nullptr,
            +[](GObject* source,GAsyncResult* result,gpointer data){
                std::unique_ptr<std::shared_ptr<Life>> weak(static_cast<std::shared_ptr<Life>*>(data));
                GError* error{};GVariant* reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&error);bool available{};
                if(reply){GVariant* boxed{};g_variant_get(reply,"(@v)",&boxed);GVariant* value=g_variant_get_variant(boxed);
                    available=g_variant_is_of_type(value,G_VARIANT_TYPE_BOOLEAN)&&g_variant_get_boolean(value);
                    g_variant_unref(value);g_variant_unref(boxed);g_variant_unref(reply);}
                if(auto* self=(*weak)->owner)self->setReady(available);
                g_clear_error(&error);
            },new std::shared_ptr<Life>(life));
    }
    GVariant* pixmaps(bool empty=false){
        GVariantBuilder result;g_variant_builder_init(&result,G_VARIANT_TYPE("a(iiay)"));
        if(icon&&!empty)for(int size:{16,22,32,48,64}){
            GdkPixbuf* scaled=gdk_pixbuf_scale_simple(icon,size,size,GDK_INTERP_BILINEAR);
            if(!scaled)continue;
            const auto* pixels=gdk_pixbuf_read_pixels(scaled);const int stride=gdk_pixbuf_get_rowstride(scaled),channels=gdk_pixbuf_get_n_channels(scaled);
            std::vector<guint8> argb(static_cast<std::size_t>(size)*size*4);
            for(int y=0;y<size;++y)for(int x=0;x<size;++x){const auto* p=pixels+y*stride+x*channels;auto* q=argb.data()+(y*size+x)*4;
                q[0]=channels==4?p[3]:255;q[1]=p[0];q[2]=p[1];q[3]=p[2];}
            g_variant_builder_add(&result,"(ii@ay)",size,size,g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,argb.data(),argb.size(),1));
            g_object_unref(scaled);
        }
        return g_variant_builder_end(&result);
    }
    static GVariant* property(GDBusConnection*,const gchar*,const gchar*,const gchar* iface,const gchar* name,GError**,gpointer data){
        auto& self=*static_cast<Impl*>(data);const std::string key=name;
        if(std::string(iface)==notifier){
            if(key=="Category")return g_variant_new_string("Hardware");
            if(key=="Id")return g_variant_new_string("magiccontrol");
            if(key=="Title")return g_variant_new_string("MagicControl");
            if(key=="Status")return g_variant_new_string("Active");
            if(key=="WindowId")return g_variant_new_uint32(0);
            if(key=="IconName")return g_variant_new_string("com.mechrevo.magiccontrol");
            if(key=="IconPixmap")return self.pixmaps();
            if(key=="OverlayIconPixmap"||key=="AttentionIconPixmap")return self.pixmaps(true);
            if(key=="ItemIsMenu")return g_variant_new_boolean(FALSE);
            if(key=="Menu")return g_variant_new_object_path("/MenuBar");
            if(key=="ToolTip"){const auto detail=ui::translate("Управление ноутбуком");return g_variant_new("(s@a(iiay)ss)","com.mechrevo.magiccontrol",self.pixmaps(),"MagicControl",detail.c_str());}
            return g_variant_new_string("");
        }
        if(key=="Version")return g_variant_new_uint32(3);
        if(key=="TextDirection")return g_variant_new_string("ltr");
        if(key=="Status")return g_variant_new_string("normal");
        if(key=="IconThemePath")return g_variant_new_strv(nullptr,0);
        return nullptr;
    }
    void event(int id,const char* name){if(std::string(name)!="clicked")return;if(id==1&&open)open();else if(id==17&&exit)exit();}
    static void method(GDBusConnection*,const gchar*,const gchar*,const gchar* iface,const gchar* name,GVariant* args,GDBusMethodInvocation* invocation,gpointer data){
        auto& self=*static_cast<Impl*>(data);const std::string methodName=name;
        if(std::string(iface)==notifier){
            if((methodName=="Activate"||methodName=="SecondaryActivate"||methodName=="ContextMenu")&&self.open)self.open();
            g_dbus_method_invocation_return_value(invocation,nullptr);return;
        }
        if(methodName=="GetLayout"){
            gint id{},depth{};GVariant* names{};g_variant_get(args,"(ii@as)",&id,&depth,&names);g_variant_unref(names);
            g_dbus_method_invocation_return_value(invocation,g_variant_new("(u@(ia{sv}av))",self.revision,layout(id,depth)));return;
        }
        if(methodName=="GetGroupProperties"){
            GVariant *ids{},*names{};g_variant_get(args,"(@ai@as)",&ids,&names);
            GVariantBuilder result;g_variant_builder_init(&result,G_VARIANT_TYPE("a(ia{sv})"));
            if(g_variant_n_children(ids)==0){for(int id=0;id<static_cast<int>(entries.size());++id)g_variant_builder_add(&result,"(i@a{sv})",id,properties(id));}
            else {GVariantIter iterator;g_variant_iter_init(&iterator,ids);gint id{};while(g_variant_iter_next(&iterator,"i",&id))
                if(id>=0&&id<static_cast<int>(entries.size()))g_variant_builder_add(&result,"(i@a{sv})",id,properties(id));}
            g_variant_unref(ids);g_variant_unref(names);g_dbus_method_invocation_return_value(invocation,g_variant_new("(@a(ia{sv}))",g_variant_builder_end(&result)));return;
        }
        if(methodName=="GetProperty"){
            gint id{};const gchar* key{};g_variant_get(args,"(i&s)",&id,&key);GVariant* props=g_variant_ref_sink(properties(id));
            GVariant* value=g_variant_lookup_value(props,key,nullptr);g_variant_unref(props);
            if(value){g_dbus_method_invocation_return_value(invocation,g_variant_new("(v)",value));g_variant_unref(value);}
            else g_dbus_method_invocation_return_dbus_error(invocation,"com.canonical.dbusmenu.Error.UnknownProperty","Unknown property");
            return;
        }
        if(methodName=="Event"){
            gint id{};const gchar* event{};GVariant* value{};guint time{};g_variant_get(args,"(i&s@vu)",&id,&event,&value,&time);
            const std::string eventName=event;
            g_variant_unref(value);g_dbus_method_invocation_return_value(invocation,nullptr);self.event(id,eventName.c_str());return;
        }
        if(methodName=="EventGroup"){
            GVariant* events{};g_variant_get(args,"(@a(isvu))",&events);GVariantIter iterator;g_variant_iter_init(&iterator,events);
            gint id{};const gchar* event{};GVariant* value{};guint time{};
            std::vector<std::pair<int,std::string>> clicks;
            while(g_variant_iter_next(&iterator,"(i&s@vu)",&id,&event,&value,&time)){clicks.emplace_back(id,event);g_variant_unref(value);}
            g_variant_unref(events);g_dbus_method_invocation_return_value(invocation,g_variant_new("(@ai)",g_variant_new_array(G_VARIANT_TYPE_INT32,nullptr,0)));
            for(const auto& click:clicks){self.event(click.first,click.second.c_str());if(click.first==17)break;}return;
        }
        if(methodName=="AboutToShow"){g_dbus_method_invocation_return_value(invocation,g_variant_new("(b)",FALSE));return;}
        if(methodName=="AboutToShowGroup"){
            g_dbus_method_invocation_return_value(invocation,g_variant_new("(@ai@ai)",g_variant_new_array(G_VARIANT_TYPE_INT32,nullptr,0),g_variant_new_array(G_VARIANT_TYPE_INT32,nullptr,0)));return;
        }
        g_dbus_method_invocation_return_dbus_error(invocation,"org.freedesktop.DBus.Error.UnknownMethod","Unknown method");
    }
};
TrayIcon::TrayIcon(std::function<void()> open,std::function<void()> exit,std::function<void(bool)> changed)
    :impl_(std::make_unique<Impl>(std::move(open),std::move(exit),std::move(changed))){}
TrayIcon::~TrayIcon()=default;
bool TrayIcon::available()const{return impl_->ready;}
void TrayIcon::refreshLanguage(){impl_->refreshLanguage();}
}
