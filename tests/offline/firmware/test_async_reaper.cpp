// Production reaper ownership with injected fake child, launch and detach.
// No native process, vendor utility, hardware, driver, GPU or user files.
#include "MagicControl/AsyncReaper.h"
#include "MagicControl/TelemetryGate.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace {
using namespace std::chrono_literals;
void check(bool ok,const char* detail){if(!ok)throw std::runtime_error(detail);}
bool eventuallyOpen(mc::TelemetryGate& gate){
    const auto deadline=std::chrono::steady_clock::now()+3s;
    do{if(gate.tryObservation())return true;std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);
    return false;
}
struct Release {
    std::promise<void>& signal;bool sent{};
    void now(){if(!sent){sent=true;signal.set_value();}}
    ~Release(){try{now();}catch(...){}}
};

enum class Failure { None, Launch, Detach, DetachNoOp, WaitOnce };
void exercise(Failure failure){
    mc::TelemetryGate gate;
    auto pause=gate.beginFirmwareCapture({},1s);
    std::promise<void> childExit,entered;auto released=childExit.get_future().share();auto enteredFuture=entered.get_future();
    std::atomic<unsigned> waits{},detachCalls{};
    auto fakeWait=[hold=pause,released,&entered,&waits,failure]() mutable {
        const auto attempt=++waits;
        if(attempt==1)entered.set_value();
        if(failure==Failure::WaitOnce&&attempt==1)throw std::runtime_error("synthetic wait failure");
        released.wait();
    };
    pause.reset();
    auto launch=[failure](auto task)->std::thread{
        if(failure==Failure::Launch)throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
        return std::thread(std::move(task));
    };
    auto detach=[failure,&detachCalls](std::thread& thread){
        ++detachCalls;
        if(failure==Failure::Detach)throw std::system_error(std::make_error_code(std::errc::invalid_argument));
        if(failure!=Failure::DetachNoOp)thread.detach();
    };
    auto caller=std::async(std::launch::async,[task=std::move(fakeWait),launch,detach]()mutable{
        mc::detail::startReaper(std::move(task),launch,detach);
    });
    Release unblock{childExit};
    check(enteredFuture.wait_for(3s)==std::future_status::ready,"fake reaper did not enter wait");
    check(!gate.tryObservation(),"reaper must retain firmware pause while fake child is alive");
    if(failure==Failure::Launch||failure==Failure::Detach||failure==Failure::DetachNoOp)
        check(caller.wait_for(0ms)==std::future_status::timeout,"failure fallback must wait for the already-running child");
    if(failure==Failure::WaitOnce){
        const auto deadline=std::chrono::steady_clock::now()+3s;
        while(waits.load()<2&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        check(waits>=2&&!gate.tryObservation(),"a wait error must retry without releasing the pause");
    }
    unblock.now();
    check(caller.wait_for(3s)==std::future_status::ready,"reaper caller did not finish after fake child exit");caller.get();
    check(eventuallyOpen(gate),"reaper did not release pause after fake child exit");
    check(waits==(failure==Failure::WaitOnce?2u:1u),"a failure path ran duplicate waits");
    check(detachCalls==(failure==Failure::Launch?0u:1u),"unexpected detach invocation count");
}
}
int main(){try{
    for(const auto mode:{Failure::None,Failure::Launch,Failure::Detach,Failure::DetachNoOp,Failure::WaitOnce})exercise(mode);
    std::cout<<"PASS common async reaper: live-child ownership, launch/detach failure and wait retry\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL async reaper: "<<error.what()<<'\n';return 1;}}
