#include "MagicControl/DeviceOperation.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

// The gate is tested in isolation without application logging, platform
// initialization or an EC session. The real audit implementation is covered
// by the application and integration tests, not replaced in product builds.
namespace mc::audit {
Id capture() noexcept { return 0; }
void related(Id,std::string_view,std::string_view) noexcept {}
}

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){
    try{
        mc::DeviceOperationGate gate,other;
        std::atomic<unsigned> notifications{};
        gate.setNotifier([&]{++notifications;});
        check(!gate.busy()&&!gate.interactiveBusy(),"initial gate must be idle");
        // Simulate periodic colour writes: retain exclusive ownership without
        // global disable/enable notifications or a transient disabled UI state.
        for(int step=0;step<60;++step){
            auto colour=gate.tryBegin(mc::DeviceOperationKind::Background);
            check(bool(colour)&&gate.owns(colour)&&!other.owns(colour),"background ownership");
            check(gate.busy()&&!gate.interactiveBusy(),"background must not disable UI");
            check(!gate.tryBegin()&&!gate.tryBegin(mc::DeviceOperationKind::Background),"exclusive background gate");
            std::atomic<bool> rejected{};
            std::thread contender([&]{rejected=!gate.tryBegin();});contender.join();
            check(rejected.load(),"concurrent interactive writer must be rejected");
            colour.reset();
            check(!gate.busy()&&!gate.interactiveBusy(),"background release");
        }
        check(notifications==0,"colour steps must not trigger global UI notifications");
        auto command=gate.tryBegin();
        check(bool(command)&&gate.busy()&&gate.interactiveBusy(),"interactive command must still disable UI");
        check(notifications==1,"interactive begin notification");
        check(!gate.tryBegin(mc::DeviceOperationKind::Background),"background must not overlap command");
        auto shared=command;command.reset();
        check(gate.busy()&&gate.interactiveBusy()&&notifications==1,"shared command lifetime");
        shared.reset();
        check(!gate.busy()&&!gate.interactiveBusy()&&notifications==2,"interactive release notification");
        auto next=gate.tryBegin(mc::DeviceOperationKind::Background);
        check(bool(next)&&!gate.interactiveBusy(),"background works after interactive command");next.reset();
        check(notifications==2,"background release stays silent after a user command");
        gate.setNotifier({});
        std::cout<<"PASS: 60 colour reservations, no UI notifications; concurrent exclusion and interactive notifications preserved\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
