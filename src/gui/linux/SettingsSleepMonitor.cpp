#include "SettingsSleepMonitor.h"
#include <utility>

namespace mc::gtkui {
SettingsSleepMonitor::SettingsSleepMonitor(GDBusConnection* bus,std::function<void(bool)> onTransition)
    :bus_(bus?G_DBUS_CONNECTION(g_object_ref(bus)):nullptr),onTransition_(std::move(onTransition)) {
    if(bus_)subscription_=g_dbus_connection_signal_subscribe(bus_,"org.freedesktop.login1",
        "org.freedesktop.login1.Manager","PrepareForSleep","/org/freedesktop/login1",nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,+[](GDBusConnection*,const gchar*,const gchar*,const gchar*,
            const gchar*,GVariant* args,gpointer owner){
            auto& self=*static_cast<SettingsSleepMonitor*>(owner);
            if(!g_variant_is_of_type(args,G_VARIANT_TYPE("(b)")))return;
            gboolean asleep{};g_variant_get(args,"(b)",&asleep);
            if(self.onTransition_)self.onTransition_(asleep!=FALSE);
        },this,nullptr);
}
SettingsSleepMonitor::~SettingsSleepMonitor(){
    if(subscription_)g_dbus_connection_signal_unsubscribe(bus_,subscription_);
    if(bus_)g_object_unref(bus_);
}
}
