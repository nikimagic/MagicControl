// Real common Linux SETs admission/worker with fake settings and readback.
// No platform backend, filesystem settings, GPU, EC or firmware access.
#include "SettingsWorkerLinux.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace mc::audit {
Id capture()noexcept{return 0;}
void related(Id,std::string_view,std::string_view)noexcept{}
}
namespace {
using namespace std::chrono_literals;
void check(bool ok,const char* detail){if(!ok)throw std::runtime_error(detail);}
bool waitUntilPaused(){const auto deadline=std::chrono::steady_clock::now()+3s;
    do{if(!mc::telemetryGate().tryObservation())return true;std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);return false;}
}
int main(){try{
    using namespace mc::linux_platform;
    SettingsWorkerRequest request;request.upper=85;request.keyboardLevel=1;request.hasBackend=true;
    std::vector<int> actions;
    SettingsWorkerActions fake{[&]{actions.push_back(1);return std::string("battery");},
        [&]{actions.push_back(2);return SettingsWorkerResult{true,"lighting"};},
        [&]{actions.push_back(3);},[&]{actions.push_back(4);return true;}};
    auto pause=mc::telemetryGate().beginFirmwareCapture({},1s);
    const auto refused=executeSettingsResourceLinux(request,fake);
    check(!refused.success&&actions.empty(),"paused SETs write must refuse before every fake read/write");
    pause.reset();
    const auto applied=executeSettingsResourceLinux(request,fake);
    check(applied.success&&actions==std::vector<int>({1,2,3,4}),"SETs write/readback order must resume unchanged");

    actions.clear();auto owner=mc::deviceOperations().tryBegin();
    check(bool(owner),"fixture could not acquire existing operation");
    const auto shared=executeSettingsResourceLinux(request,fake,owner);
    check(shared.success&&actions==std::vector<int>({1,2,3,4}),"already-owned SETs operation must not self-deadlock");owner.reset();

    actions.clear();mc::DeviceOperationGate foreignGate;auto foreign=foreignGate.tryBegin();
    check(bool(foreign),"fixture could not acquire foreign operation");
    const auto foreignResult=executeSettingsResourceLinux(request,fake,foreign);foreign.reset();
    check(!foreignResult.success&&actions.empty(),"foreign operation must not admit SETs writes");

    actions.clear();std::promise<void> queued,dispatch;auto queuedFuture=queued.get_future();auto readyToDispatch=dispatch.get_future().share();
    auto delayed=std::async(std::launch::async,[&]{queued.set_value();readyToDispatch.wait();return executeSettingsResourceLinux(request,fake);});
    struct UnblockQueued {std::promise<void>& signal;bool sent{};void now(){if(!sent){sent=true;signal.set_value();}}~UnblockQueued(){try{now();}catch(...){}}} unblockQueued{dispatch};
    check(queuedFuture.wait_for(3s)==std::future_status::ready,"queued fake SETs job did not start");
    auto queuedPause=mc::telemetryGate().requestFirmwareCapture();unblockQueued.now();
    const auto delayedResult=delayed.get();
    check(!delayedResult.success&&actions.empty(),"SETs request queued before firmware must recheck admission at execution");
    queuedPause.reset();

    std::promise<void> entered,release;auto enteredFuture=entered.get_future();auto allowed=release.get_future().share();
    std::atomic<unsigned> writes{},observations{};
    SettingsWorkerActions slow{[&]{++writes;entered.set_value();allowed.wait();return std::string("battery");},
        [&]{++writes;return SettingsWorkerResult{true,"lighting"};},[&]{++observations;},[&]{++observations;return true;}};
    auto work=std::async(std::launch::async,[&]{return executeSettingsResourceLinux(request,slow);});
    struct Unblock {std::promise<void>& signal;bool sent{};void now(){if(!sent){sent=true;signal.set_value();}}~Unblock(){try{now();}catch(...){}}} unblock{release};
    check(enteredFuture.wait_for(3s)==std::future_status::ready,"in-flight fake SETs write did not start");
    std::stop_source stop;auto capture=std::async(std::launch::async,[&]{return mc::telemetryGate().beginFirmwareCapture(stop.get_token(),5s);});
    const bool closed=waitUntilPaused(),waiting=capture.wait_for(0ms)==std::future_status::timeout;
    actions.clear();const auto denied=executeSettingsResourceLinux(request,fake);
    unblock.now();const auto completed=work.get();
    const bool drained=capture.wait_for(3s)==std::future_status::ready;if(!drained)stop.request_stop();auto ticket=capture.get();
    check(closed&&waiting&&!denied.success&&actions.empty()&&completed.success&&writes==2&&observations==2&&drained&&ticket,
        "firmware must drain the entire in-flight SETs write plus readback and reject later SETs");
    ticket.reset();
    std::cout<<"PASS Linux SETs resource: pause refusal, owned/foreign admission and in-flight write/readback drain\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL Linux SETs guard: "<<error.what()<<'\n';return 1;}}
