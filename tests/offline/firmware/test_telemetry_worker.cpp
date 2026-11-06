// Exercises the production ResourceWorker and process-local TelemetryGate.
// Tasks, devices, backend calls and notifications are synthetic.
#include "MagicControl/ResourceControl.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace mc::audit {
TaskContext::TaskContext()noexcept=default;
TaskContext::~TaskContext()=default;
Id capture()noexcept{return 0;}
void related(Id,std::string_view,std::string_view)noexcept{}
}
namespace {
using namespace std::chrono_literals;
void check(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
bool waitUntilPaused(){
    const auto deadline=std::chrono::steady_clock::now()+3s;
    do{if(!mc::telemetryGate().tryObservation())return true;std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);
    return false;
}

void queuedWorkRechecksAdmission(){
    std::promise<void> allow,ready;auto run=allow.get_future().share();auto completion=ready.get_future();
    mc::ResourceWorker worker([run](std::function<void()> body){return std::thread([run,body=std::move(body)]{run.wait();body();});});
    std::atomic<unsigned> reads{},skips{},notices{};int lastDisplay=41;
    worker.launch([&]{++reads;lastDisplay=0;},[&]{++notices;ready.set_value();},mc::ResourceWorkClass::Telemetry,[&]{++skips;});
    struct Release{std::promise<void>& signal;bool sent{};void now(){if(!sent){sent=true;signal.set_value();}}~Release(){try{now();}catch(...){}}} release{allow};
    std::stop_source stop;auto pause=mc::telemetryGate().beginFirmwareCapture(stop.get_token(),1s);
    release.now();
    const bool completed=completion.wait_for(3s)==std::future_status::ready;
    if(!completed)pause.reset();
    check(completed&&worker.takeCompletion(),"queued fake telemetry did not report completion");
    check(worker.telemetrySkipped()&&reads==0&&skips==1&&notices==1&&lastDisplay==41,
        "queued telemetry must skip without an empty replacement or missing completion");
    pause.reset();
    std::promise<void> secondReady;auto second=secondReady.get_future();
    worker.launch([&]{++reads;lastDisplay=42;},[&]{secondReady.set_value();},mc::ResourceWorkClass::Telemetry);
    check(second.wait_for(3s)==std::future_status::ready&&worker.takeCompletion(),"telemetry did not resume");
    check(!worker.telemetrySkipped()&&reads==1&&lastDisplay==42,"next telemetry sample must be fresh");
}

void activeWorkDrainsAndInteractiveStillRuns(){
    mc::ResourceWorker active,second,interactive;
    std::promise<void> entered,release,activeReady,secondReady,interactiveReady;
    auto released=release.get_future().share();auto enteredFuture=entered.get_future();
    auto activeDone=activeReady.get_future(),secondDone=secondReady.get_future(),interactiveDone=interactiveReady.get_future();
    std::atomic<unsigned> oldReads{},newReads{},interactiveCalls{};
    active.launch([&]{++oldReads;entered.set_value();released.wait();},[&]{activeReady.set_value();},mc::ResourceWorkClass::Telemetry);
    struct Release{std::promise<void>& signal;bool sent{};void now(){if(!sent){sent=true;signal.set_value();}}~Release(){try{now();}catch(...){}}} releaseActive{release};
    check(enteredFuture.wait_for(3s)==std::future_status::ready,"fake active telemetry did not start");
    std::stop_source stop;
    auto waiter=std::async(std::launch::async,[&]{return mc::telemetryGate().beginFirmwareCapture(stop.get_token(),5s);});
    const bool paused=waitUntilPaused();
    const bool waiting=waiter.wait_for(0ms)==std::future_status::timeout;
    second.launch([&]{++newReads;},[&]{secondReady.set_value();},mc::ResourceWorkClass::Telemetry);
    const bool secondCompleted=secondDone.wait_for(3s)==std::future_status::ready;
    interactive.launch([&]{++interactiveCalls;},[&]{interactiveReady.set_value();},mc::ResourceWorkClass::Interactive);
    const bool interactiveCompleted=interactiveDone.wait_for(3s)==std::future_status::ready;
    releaseActive.now();
    const bool drained=waiter.wait_for(3s)==std::future_status::ready;
    if(!drained)stop.request_stop();
    auto pause=waiter.get();
    check(activeDone.wait_for(3s)==std::future_status::ready&&active.takeCompletion(),"in-flight fake telemetry did not finish");
    check(secondCompleted&&second.takeCompletion()&&second.telemetrySkipped(),"new fake telemetry was not skipped");
    check(interactiveCompleted&&interactive.takeCompletion(),"interactive non-telemetry task was starved");
    check(paused&&waiting&&drained&&bool(pause)&&oldReads==1&&newReads==0&&interactiveCalls==1,
        "capture must await the active task without starting another observation");
    pause.reset();
}

void backgroundDeviceOperationDrains(){
    auto background=mc::deviceOperations().tryBegin(mc::DeviceOperationKind::Background);
    check(bool(background),"fake background DeviceOperation did not start");
    std::stop_source stop;
    auto capture=std::async(std::launch::async,[&]{return mc::telemetryGate().beginFirmwareCapture(stop.get_token(),5s);});
    const bool paused=waitUntilPaused();
    const bool waiting=capture.wait_for(0ms)==std::future_status::timeout;
    background.reset();
    const bool drained=capture.wait_for(3s)==std::future_status::ready;
    if(!drained)stop.request_stop();
    auto pause=capture.get();
    check(paused&&waiting&&drained&&bool(pause),"capture must await an already-admitted background DeviceOperation");
    check(!mc::deviceOperations().tryBegin(mc::DeviceOperationKind::Background),"new background DeviceOperation must be refused while paused");
    pause.reset();
    check(bool(mc::deviceOperations().tryBegin(mc::DeviceOperationKind::Background)),"background DeviceOperation must resume after capture");
}

void firmwareTicketAdmission(){
    for(const auto kind:{mc::DeviceOperationKind::Background,mc::DeviceOperationKind::Interactive}){
        auto prior=mc::deviceOperations().tryBegin(kind);
        check(bool(prior),"fake pre-existing DeviceOperation did not start");
        auto ticket=mc::telemetryGate().requestFirmwareCapture();
        check(bool(ticket)&&!mc::deviceOperations().tryBegin(kind),"new device operation must be rejected after firmware request");
        check(!mc::deviceOperations().tryBeginFirmware(ticket),"firmware must not reserve device before observation drain");
        std::stop_source stop;
        auto drained=std::async(std::launch::async,[&]{ticket->waitForQuiescence(stop.get_token(),5s);});
        const bool waited=drained.wait_for(0ms)==std::future_status::timeout;
        prior.reset();
        const bool completed=drained.wait_for(3s)==std::future_status::ready;
        if(!completed)stop.request_stop();
        drained.get();
        auto owner=mc::deviceOperations().tryBeginFirmware(ticket);
        check(waited&&completed&&owner&&mc::deviceOperations().owns(owner),"firmware ticket must enter once prior device operation is drained");
        check(!mc::deviceOperations().tryBeginFirmware(ticket),"firmware ticket cannot duplicate active device ownership");
        owner.reset();ticket.reset();
        check(bool(mc::deviceOperations().tryBegin(kind)),"normal device operation did not resume after firmware completion");
    }
    mc::TelemetryGate foreignGate;auto foreign=foreignGate.beginFirmwareCapture({},1s);
    check(!mc::deviceOperations().tryBeginFirmware(foreign),"foreign firmware ticket must not admit the process device gate");
}
}
int main(){try{
    queuedWorkRechecksAdmission();activeWorkDrainsAndInteractiveStillRuns();backgroundDeviceOperationDrains();firmwareTicketAdmission();
    std::cout<<"PASS real ResourceWorker and background DeviceOperation: queued skip, completion, display preservation and in-flight drain\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL telemetry worker: "<<error.what()<<'\n';return 1;}}
