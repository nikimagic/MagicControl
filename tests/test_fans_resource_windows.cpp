#include "MagicControl/Logging.h"
#include "../src/gui/windows/FansPage.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

int main(int argc,char** argv){
    if(argc!=2)return 2;
    const bool enabled=std::string(argv[1])=="enabled";
    const auto directory=std::filesystem::current_path()/
        ("fans-resource-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!mc::initializeLogging(true,directory,false))return 3;
    const auto logPath=mc::currentLogPath();
    const HWND parent=CreateWindowExW(0,L"STATIC",L"FANs resource test",0,0,0,0,0,HWND_MESSAGE,nullptr,nullptr,nullptr);
    if(!parent)return 4;
    std::atomic<int> samples{},phase{};bool observed=false,changedObserved=false;
    {
        auto source=[&]{++samples;mc::MonitoringSnapshot snapshot;const bool changed=phase.load()!=0;
            snapshot.processors[0].fanRpm=1000;snapshot.processors[0].fanPwm=25;snapshot.processors[0].fanSource="fixture cpu fan";
            snapshot.processors[1].fanRpm=changed?2200:2000;if(!changed)snapshot.processors[1].fanPwm=50;
            snapshot.processors[1].fanSource="fixture second fan";
            snapshot.gpuCards.initialized=true;mc::GpuCardReading gpu;gpu.adapterId="fixture-gpu";
            gpu.usagePercent=changed?20:10;snapshot.gpuCards.cards.push_back(gpu);
            return snapshot;};
        mc::gui::FansPage page(parent,nullptr,nullptr,{}, {},source);
        page.arrange(RECT{0,0,1600,1200},96,true);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(std::chrono::steady_clock::now()<until){
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            std::ifstream file(logPath,std::ios::binary);
            const std::string log{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
            const std::string expected="\"fan_resource_used\":[{\"speed\":true,\"duty\":true},{\"speed\":true,\"duty\":true}]";
            if(log.find(expected)!=std::string::npos){observed=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if(observed){
            phase=1;const auto changedUntil=std::chrono::steady_clock::now()+std::chrono::seconds(8);
            while(std::chrono::steady_clock::now()<changedUntil){
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                std::ifstream file(logPath,std::ios::binary);
                const std::string log{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
                const auto start=log.rfind("\"source\":\"FANs.presentation\"");
                const std::string expected="\"fan_resource_used\":[{\"speed\":true,\"duty\":true},{\"speed\":true,\"duty\":false}]";
                if(start!=std::string::npos&&log.find("\"load\":20",start)!=std::string::npos&&log.find(expected,start)!=std::string::npos){changedObserved=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    DestroyWindow(parent);mc::initializeLogging(false);
    std::error_code ignored;std::filesystem::remove(logPath,ignored);std::filesystem::remove(directory,ignored);
    std::cout<<"FANs resource UI enabled="<<enabled<<" samples="<<samples.load()<<" observed="<<observed<<" changed="<<changedObserved<<"; injected monitor, no EC backend\n";
    return observed&&changedObserved&&samples.load()>=2?0:1;
}
