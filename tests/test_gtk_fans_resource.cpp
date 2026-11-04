#include <gtk/gtk.h>
#include "../src/gui/linux/Pages.h"
#include "../src/gui/linux/Ui.h"
#include "MagicControl/Logging.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc,char** argv){
    if(argc!=2)return 2;
    if(!gtk_init_check()){std::puts("GTK display unavailable: skipped");return 77;}
    g_log_set_always_fatal(static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR|G_LOG_LEVEL_CRITICAL));
    const bool enabled=std::string(argv[1])=="enabled";
    const auto directory=std::filesystem::current_path()/
        ("fans-resource-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!mc::initializeLogging(true,directory,false))return 3;
    const auto logPath=mc::currentLogPath();std::atomic<int> samples{},phase{};
    auto source=[&]{++samples;mc::MonitoringSnapshot s;const bool changed=phase.load()!=0;
        s.processorSampledAtMs=static_cast<std::uint64_t>(g_get_monotonic_time()/1000);
        s.processors[0].fanRpm=1000;s.processors[0].fanPwm=25;s.processors[0].fanSource="fixture cpu fan";
        s.processors[1].fanRpm=changed?2200:2000;if(!changed)s.processors[1].fanPwm=50;
        s.processors[1].fanSource="fixture second fan";
        s.gpuCards.initialized=true;mc::GpuCardReading gpu;gpu.adapterId="fixture-gpu";
        gpu.usagePercent=changed?20:10;s.gpuCards.cards.push_back(gpu);return s;};
    auto* window=gtk_window_new();g_object_ref_sink(window);mc::gtkui::installUiCss(window);
    auto* page=mc::gtkui::createFansPage(nullptr,source);gtk_window_set_child(GTK_WINDOW(window),page);
    gtk_window_set_default_size(GTK_WINDOW(window),1400,1000);gtk_window_present(GTK_WINDOW(window));
    bool observed=false,changedObserved=false;const auto until=g_get_monotonic_time()+8000000;
    const std::string expected="\"fan_resource_used\":[{\"speed\":true,\"duty\":true},{\"speed\":true,\"duty\":true}]";
    while(g_get_monotonic_time()<until){
        while(g_main_context_iteration(nullptr,FALSE)){}
        std::ifstream file(logPath,std::ios::binary);
        const std::string log{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
        if(log.find(expected)!=std::string::npos){observed=true;break;}
        g_usleep(10000);
    }
    if(observed){
        phase=1;const auto changedUntil=g_get_monotonic_time()+8000000;
        while(g_get_monotonic_time()<changedUntil){
            while(g_main_context_iteration(nullptr,FALSE)){}
            std::ifstream file(logPath,std::ios::binary);
            const std::string log{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
            const auto start=log.rfind("\"source\":\"FANs.presentation\"");
            if(start!=std::string::npos&&log.find("\"load\":20",start)!=std::string::npos&&
               log.find("\"field\":\"processor.1.pwm\"",start)!=std::string::npos&&
               log.find(expected,start)!=std::string::npos){changedObserved=true;break;}
            g_usleep(10000);
        }
    }
    gtk_window_destroy(GTK_WINDOW(window));g_object_unref(window);
    while(g_main_context_iteration(nullptr,FALSE)){}
    mc::initializeLogging(false);
    std::error_code ignored;std::filesystem::remove(logPath,ignored);std::filesystem::remove(directory,ignored);
    std::cout<<"GTK FANs resource enabled="<<enabled<<" samples="<<samples.load()<<" observed="<<observed<<" changed="<<changedObserved<<"; injected monitor, no EC backend\n";
    return observed&&changedObserved&&samples.load()>=2?0:1;
}
