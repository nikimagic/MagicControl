#include "../src/gui/linux/SettingsSleepMonitor.h"
#include "../src/platform/linux/SettingsObservationEpoch.h"
#include <gio/gio.h>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(){
    const auto check=[](bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);};
    auto* daemon=g_test_dbus_new(G_TEST_DBUS_NONE);g_test_dbus_up(daemon);
    GError* error{};const auto flags=static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION);
    auto* sender=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(daemon),flags,nullptr,nullptr,&error);
    check(sender&&!error,"create fake sender");
    auto* listener=g_dbus_connection_new_for_address_sync(g_test_dbus_get_bus_address(daemon),flags,nullptr,nullptr,&error);
    check(listener&&!error,"create listener");
    auto* name=g_dbus_connection_call_sync(sender,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","RequestName",
        g_variant_new("(su)","org.freedesktop.login1",0u),G_VARIANT_TYPE("(u)"),G_DBUS_CALL_FLAGS_NONE,3000,nullptr,&error);
    check(name&&!error,"acquire simulated login1 name");guint32 acquired{};g_variant_get(name,"(u)",&acquired);g_variant_unref(name);check(acquired==1,"login1 name acquired");
    std::vector<bool> events;mc::linux_platform::SettingsObservationEpoch epoch;
    const auto emit=[&](GVariant* args){check(g_dbus_connection_emit_signal(sender,nullptr,"/org/freedesktop/login1",
        "org.freedesktop.login1.Manager","PrepareForSleep",args,&error)&&!error,"emit test signal");
        check(g_dbus_connection_flush_sync(sender,nullptr,&error)&&!error,"flush test signal");};
    const auto pump=[&](std::size_t count){const auto deadline=g_get_monotonic_time()+3000000;
        while(events.size()<count&&g_get_monotonic_time()<deadline){while(g_main_context_iteration(nullptr,FALSE)){}g_usleep(1000);} };
    {
        mc::gtkui::SettingsSleepMonitor monitor(listener,[&](bool asleep){events.push_back(asleep);epoch.transition(asleep);});
        auto* barrier=g_dbus_connection_call_sync(listener,"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus.Peer","Ping",
            nullptr,nullptr,G_DBUS_CALL_FLAGS_NONE,3000,nullptr,&error);
        check(barrier&&!error,"subscription barrier");g_variant_unref(barrier);
        const auto old=epoch.capture();emit(g_variant_new("(b)",TRUE));pump(1);
        check(events==std::vector<bool>{true}&&epoch.asleep()&&!epoch.current(old),"sleep delivery and invalidation");
        emit(g_variant_new("(b)",FALSE));pump(2);
        check(events==std::vector<bool>({true,false})&&!epoch.asleep()&&!epoch.current(old),"resume delivery");
        emit(g_variant_new("(s)","bad"));
        const auto deadline=g_get_monotonic_time()+100000;while(g_get_monotonic_time()<deadline){while(g_main_context_iteration(nullptr,FALSE)){}g_usleep(1000);}
        check(events.size()==2,"malformed signal ignored");
    }
    emit(g_variant_new("(b)",TRUE));
    const auto deadline=g_get_monotonic_time()+100000;while(g_get_monotonic_time()<deadline){while(g_main_context_iteration(nullptr,FALSE)){}g_usleep(1000);}
    check(events.size()==2,"no callback after unsubscribe");
    g_dbus_connection_close_sync(listener,nullptr,nullptr);g_dbus_connection_close_sync(sender,nullptr,nullptr);
    g_object_unref(listener);g_object_unref(sender);g_test_dbus_down(daemon);g_object_unref(daemon);
    std::cout<<"SETs private D-Bus sleep checks passed\n";
}
