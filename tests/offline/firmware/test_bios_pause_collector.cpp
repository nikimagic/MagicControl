// Real common BIOS/INFO entry points linked ONLY to fake platform collectors.
// No Windows/Linux reader, vendor utility, driver, EC, GPU or user files.
#include "MagicControl/BiosRomDump.h"
#include "MagicControl/InfoResourceControl.h"
#include "MagicControl/ActionLog.h"
#include "MagicControl/TelemetryGate.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

// One-shot allocator fault restricted to the fixture's calling thread. This
// exercises real INFO submission failure without changing the product executor.
thread_local int fixtureAllocationCountdown=-1;
void* operator new(std::size_t size){
    if(fixtureAllocationCountdown>=0&&fixtureAllocationCountdown--==0){fixtureAllocationCountdown=-1;throw std::bad_alloc();}
    if(void* result=std::malloc(size?size:1))return result;
    throw std::bad_alloc();
}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete(void* value,std::size_t)noexcept{std::free(value);}

namespace {
using namespace std::chrono_literals;
enum class Mode { Success, Pending, PendingThrow, PendingUntilCancelled, Throw };
std::atomic<Mode> mode{Mode::Success};
std::atomic<unsigned> platformCalls{},terminalResults{};
std::atomic<bool> pendingStarted{};
std::mutex childGate;
std::shared_ptr<mc::BiosCaptureSession> syntheticChild;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
[[noreturn]] void forbidden(){throw std::runtime_error("offline BIOS fixture called an unrelated collector");}
bool eventuallyPaused(){
    const auto until=std::chrono::steady_clock::now()+3s;
    do{if(!mc::telemetryGate().tryObservation())return true;std::this_thread::yield();}while(std::chrono::steady_clock::now()<until);
    return false;
}
void fakeChildExit(){std::shared_ptr<mc::BiosCaptureSession> child;{std::lock_guard lock(childGate);child=std::move(syntheticChild);}child.reset();}
struct HeldPool {
    std::mutex gate;std::condition_variable changed;unsigned entered{};bool released{};
    void block(){std::unique_lock lock(gate);++entered;changed.notify_all();changed.wait(lock,[&]{return released;});}
    bool enteredCount(unsigned count){std::unique_lock lock(gate);return changed.wait_for(lock,5s,[&]{return entered>=count;});}
    void unblock(){std::lock_guard lock(gate);released=true;changed.notify_all();}
};
void queuedInfoCancellation(){
    auto blockers=std::make_shared<HeldPool>();
    struct EnsureRelease {std::shared_ptr<HeldPool> pool;~EnsureRelease(){pool->unblock();}} ensure{blockers};
    for(int i=0;i<3;++i)mc::submitDetachedResource([blockers]{blockers->block();},mc::ResourceWorkClass::Observation);
    check(blockers->enteredCount(3),"fixture could not occupy the three observation workers");
    mc::submitDetachedResource([blockers]{blockers->block();},mc::ResourceWorkClass::Interactive);
    check(blockers->enteredCount(4),"fixture could not occupy the reserved interactive worker");

    const auto callsBefore=platformCalls.load(),resultsBefore=terminalResults.load();
    std::vector<std::weak_ptr<mc::InfoOperation>> retired;
    for(const auto kind:{mc::InfoResourceOperation::BiosDump,mc::InfoResourceOperation::VbiosDump}){
        mc::InfoOperationRequest request;request.operation=kind;
        auto job=mc::InfoOperation::start(std::move(request));
        check(!job->done()&&!mc::telemetryGate().tryObservation(),"INFO must close admission synchronously before queued firmware worker starts");
        job->cancel();
        const auto* result=job->poll();
        check(job->done()&&result&&result->cancelled&&!result->success,"pre-start cancellation must publish a cancelled result");
        check(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"pre-start cancellation must release the unused ticket");
        retired.emplace_back(job);job.reset();
    }
    check(platformCalls==callsBefore&&terminalResults==resultsBefore+2,"cancelled queued firmware jobs must not call a collector or publish twice");

    // Find an injected allocation failure after ticket acquisition but before
    // worker execution. With all workers held, an already-done job in this
    // generation can only have failed while creating/submitting the worker.
    bool submitFailureObserved=false;
    for(int failurePoint=0;failurePoint<128&&!submitFailureObserved;++failurePoint){
        auto beforeObservation=mc::telemetryGate().tryObservation();
        check(bool(beforeObservation),"allocation fixture expected open gate");
        const auto generation=beforeObservation->generation();beforeObservation.reset();
        mc::InfoOperationRequest request;request.operation=mc::InfoResourceOperation::BiosDump;
        std::shared_ptr<mc::InfoOperation> job;
        try{fixtureAllocationCountdown=failurePoint;job=mc::InfoOperation::start(std::move(request));fixtureAllocationCountdown=-1;}
        catch(const std::bad_alloc&){fixtureAllocationCountdown=-1;}
        catch(...){fixtureAllocationCountdown=-1;throw;}
        if(job){
            const bool failedBeforeWorker=job->done();
            if(!failedBeforeWorker)job->cancel();
            const auto* outcome=job->poll();
            check(job->done()&&outcome&&!outcome->success,"failed or cancelled submission must publish a failure");
            auto afterObservation=mc::telemetryGate().tryObservation();
            check(bool(afterObservation)&&!mc::deviceOperations().busy(),"failed submission must release its telemetry ticket");
            submitFailureObserved=failedBeforeWorker&&afterObservation->generation()!=generation;
            retired.emplace_back(job);job.reset();
        }else check(bool(mc::telemetryGate().tryObservation()),"allocation failure before INFO creation must not strand admission");
    }
    check(submitFailureObserved,"fixture did not reach a worker-submission allocation failure after acquiring a ticket");
    const auto resultsAfterFailures=terminalResults.load();
    check(platformCalls==callsBefore,"allocation failure must not run a fake collector");
    blockers->unblock();
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::any_of(retired.begin(),retired.end(),[](const auto& weak){return !weak.expired();})&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    check(std::all_of(retired.begin(),retired.end(),[](const auto& weak){return weak.expired();}),"cancelled queued firmware workers did not retire");
    check(platformCalls==callsBefore&&terminalResults==resultsAfterFailures,"late worker start must not run a collector or republish a result");
}
struct FakeInventory final:mc::IHardwareProvider {
    mutable std::atomic<unsigned> calls{};
    const char* id()const noexcept override{return "synthetic";}
    const char* description()const noexcept override{return "in-memory fixture";}
    mc::HardwareInventory collect(std::stop_token)const override{++calls;mc::HardwareInventory out;out.readAt="synthetic";return out;}
};
void inventoryAdmission(){
    FakeInventory provider;
    (void)mc::collectInfoInventoryResource({},&provider);
    check(provider.calls==1,"direct INFO inventory did not invoke the fake provider");
    auto pause=mc::telemetryGate().requestFirmwareCapture();
    bool refused=false;
    try{(void)mc::collectInfoInventoryResource({},&provider);}catch(const std::runtime_error&){refused=true;}
    check(refused&&provider.calls==1,"direct INFO inventory must not start during pending firmware");
    unsigned injectedCalls{},saved{};
    const auto injected=[&](std::stop_token){++injectedCalls;return mc::HardwareInventory{};};
    const auto save=[&](const mc::HardwareInventory&,std::string&){++saved;return true;};
    const auto blocked=mc::executeInfoInventoryResource({},injected,save);
    check(!blocked.saved&&injectedCalls==0&&saved==0,"injected INFO inventory must refuse before reading and saving during firmware");
    pause.reset();
    const auto available=mc::executeInfoInventoryResource({},injected,save);
    check(available.saved&&injectedCalls==1&&saved==1,"INFO inventory must resume after firmware completion");

    std::promise<void> entered,release;auto enteredFuture=entered.get_future();auto allowed=release.get_future().share();
    struct Unblock {std::promise<void>& signal;bool sent{};void now(){if(!sent){sent=true;signal.set_value();}}~Unblock(){try{now();}catch(...){}}} unblock{release};
    std::atomic<unsigned> saves{};
    mc::InfoOperationServices services;
    services.collect=[&](std::stop_token){entered.set_value();allowed.wait();mc::HardwareInventory out;out.readAt="in-memory";return out;};
    services.save=[&](const mc::HardwareInventory&,std::string&){++saves;return true;};
    mc::InfoOperationRequest request;request.operation=mc::InfoResourceOperation::Inventory;
    auto job=mc::InfoOperation::start(std::move(request),services);
    check(enteredFuture.wait_for(3s)==std::future_status::ready,"in-flight fake inventory did not start");
    auto ticket=mc::telemetryGate().requestFirmwareCapture();
    std::stop_source stop;
    auto drained=std::async(std::launch::async,[&]{ticket->waitForQuiescence(stop.get_token(),5s);});
    const bool waiting=drained.wait_for(0ms)==std::future_status::timeout;
    const auto second=mc::executeInfoInventoryResource({},injected,save);
    unblock.now();
    const bool completed=drained.wait_for(3s)==std::future_status::ready;if(!completed)stop.request_stop();
    drained.get();
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while(!job->done()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    check(waiting&&completed&&!second.saved&&injectedCalls==1&&saved==1&&job->done()&&job->poll()->success&&saves==1,
        "firmware must await in-flight INFO inventory and refuse another without blocking its eventual save");
    ticket.reset();
}
}

namespace mc {
DiagnosticDumpResult createPlatformBiosRomDump(std::stop_token token,std::atomic<std::uint64_t>*,const std::shared_ptr<BiosCaptureSession>& session){
    ++platformCalls;
    check(session&&session->pause&&session->operation&&deviceOperations().owns(session->operation),"BIOS platform must receive both real shared owners");
    check(!telemetryGate().tryObservation()&&!telemetryGate().trySafetyControl(),"BIOS platform entered without pausing background observations");
    if(mode==Mode::Throw)throw std::runtime_error("synthetic BIOS reader error");
    if(mode==Mode::Pending||mode==Mode::PendingThrow||mode==Mode::PendingUntilCancelled){
        {std::lock_guard lock(childGate);syntheticChild=session;}
        if(mode==Mode::PendingThrow)throw std::runtime_error("synthetic error after reader start");
        if(mode==Mode::PendingUntilCancelled){
            pendingStarted=true;
            const auto deadline=std::chrono::steady_clock::now()+5s;
            while(!token.stop_requested()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
            check(token.stop_requested(),"test timed out waiting for cancellation after fake reader start");
            DiagnosticDumpResult pending;pending.cancelled=true;pending.message="synthetic reader still alive after cancel";return pending;
        }
        DiagnosticDumpResult pending;pending.message="synthetic reader still alive";return pending;
    }
    DiagnosticDumpResult complete;complete.saved=true;complete.message="synthetic BIOS saved";return complete;
}
std::size_t HardwareInventory::rowCount()const noexcept{return 0;}
bool InventoryCache::save(const HardwareInventory&,std::string&){forbidden();}
const IHardwareProvider& defaultHardwareProvider(){forbidden();}
DiagnosticDumpResult createDiagnosticDump(std::shared_ptr<EcAccess>,std::stop_token,MaximumFanProbe,
    std::shared_ptr<std::mutex>,DiagnosticDumpMode,FirmwareFanDefault,std::string,std::filesystem::path){forbidden();}
DiagnosticDumpResult createPowerInputDump(PowerInputDumpSession&,const PowerInputPrompt&,
    std::stop_token,std::shared_ptr<EcAccess>,std::string){forbidden();}
VbiosRomDumpResult createVbiosRomDump(std::stop_token,std::atomic<std::uint64_t>*,std::string,std::filesystem::path,std::shared_ptr<TelemetryGate::FirmwarePause>){forbidden();}
void logMessage(std::string_view,std::string_view)noexcept{}
void recordLog(std::string_view,std::string_view)noexcept{}
namespace audit {
Id capture()noexcept{return 0;}
std::string quote(std::string_view){return "\"synthetic\"";}
void related(Id,std::string_view name,std::string_view)noexcept{if(name=="info.result")++terminalResults;}
TaskContext::TaskContext()noexcept=default;
TaskContext::~TaskContext()=default;
Scope::Scope(std::string_view command,std::string_view,Id,bool)noexcept:command_(command){}
Scope::~Scope()=default;
void Scope::finish(std::string_view,std::string_view)noexcept{}
void Scope::fail(std::string_view)noexcept{}
}
}

int main(){try{
    queuedInfoCancellation();
    inventoryAdmission();
    auto observation=mc::telemetryGate().tryObservation();
    check(bool(observation),"pre-existing fake telemetry must start");
    auto running=std::async(std::launch::async,[]{return mc::createBiosRomDump();});
    const bool blockedNew=eventuallyPaused();
    const bool waited=running.wait_for(0ms)==std::future_status::timeout;
    const auto beforeRelease=platformCalls.load();
    observation.reset();
    const auto complete=running.get();
    check(blockedNew&&waited&&beforeRelease==0&&complete.saved&&platformCalls==1,"real BIOS entry must drain before platform reader starts");
    check(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"completed BIOS releases both owners");

    auto supplied=mc::telemetryGate().requestFirmwareCapture();
    const auto withSuppliedTicket=mc::createBiosRomDump({},nullptr,supplied);
    check(withSuppliedTicket.saved&&!mc::telemetryGate().tryObservation(),"BIOS must consume the supplied ticket without reacquiring or releasing its caller's owner");
    supplied.reset();
    check(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"caller ticket release reopens BIOS observation admission");

    mc::TelemetryGate foreignGate;auto foreign=foreignGate.requestFirmwareCapture();bool foreignRejected=false;
    const auto beforeForeign=platformCalls.load();
    try{(void)mc::createBiosRomDump({},nullptr,foreign);}catch(const std::invalid_argument&){foreignRejected=true;}
    foreign.reset();
    check(foreignRejected&&platformCalls==beforeForeign&&bool(mc::telemetryGate().tryObservation()),"BIOS must refuse a foreign ticket without invoking platform");

    auto safety=mc::telemetryGate().trySafetyControl();
    check(bool(safety),"fake cooling control must start");
    bool rejected=false;
    try{(void)mc::createBiosRomDump();}catch(const std::runtime_error&){rejected=true;}
    safety.reset();
    check(rejected&&platformCalls==2&&bool(mc::telemetryGate().tryObservation()),"BIOS must refuse safety-controlled cooling before platform execution");

    auto ecDiagnostic=mc::telemetryGate().tryDiagnosticCollection();
    check(bool(ecDiagnostic),"fake EC collector must acquire diagnostic ownership");
    rejected=false;
    try{(void)mc::createBiosRomDump();}catch(const std::runtime_error&){rejected=true;}
    check(rejected&&platformCalls==2&&bool(mc::telemetryGate().tryObservation()),"real BIOS entry must refuse an active EC diagnostic without blocking its telemetry");
    ecDiagnostic.reset();

    std::stop_source cancelled;cancelled.request_stop();rejected=false;
    try{(void)mc::createBiosRomDump(cancelled.get_token());}catch(const std::runtime_error&){rejected=true;}
    check(rejected&&platformCalls==2&&bool(mc::telemetryGate().tryObservation()),"pre-cancelled BIOS must not start platform reader");

    mode=Mode::Throw;rejected=false;
    try{(void)mc::createBiosRomDump();}catch(const std::runtime_error&){rejected=true;}
    check(rejected&&platformCalls==3&&bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"platform error must unwind BIOS ownership");

    mode=Mode::Pending;
    mc::InfoDumpResourceRequest request;request.kind=mc::InfoDumpKind::Bios;
    const auto partial=mc::executeInfoDumpResource(request).report;
    check(!partial.saved&&partial.message=="synthetic reader still alive", "INFO resource must keep fake pending result");
    check(!mc::telemetryGate().tryObservation()&&mc::deviceOperations().busy(),"fake pending BIOS child retains both owners after entry returns");
    fakeChildExit();
    check(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"fake BIOS child exit releases both owners");

    mode=Mode::PendingThrow;rejected=false;
    try{(void)mc::createBiosRomDump();}catch(const std::runtime_error&){rejected=true;}
    check(rejected&&!mc::telemetryGate().tryObservation()&&mc::deviceOperations().busy(),"exception after fake reader starts must retain both owners");
    fakeChildExit();
    check(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"fake reader exit after exception releases both owners");

    mode=Mode::Pending;
    mc::InfoOperationRequest pendingInfo;pendingInfo.operation=mc::InfoResourceOperation::BiosDump;
    const auto pendingBefore=terminalResults.load();
    auto pendingJob=mc::InfoOperation::start(std::move(pendingInfo));
    const auto pendingDeadline=std::chrono::steady_clock::now()+5s;
    while(!pendingJob->done()&&std::chrono::steady_clock::now()<pendingDeadline)std::this_thread::yield();
    check(pendingJob->done(),"fake pending INFO must return control without waiting for child");
    check(pendingJob->poll()!=nullptr&&terminalResults==pendingBefore+1,"pending INFO publishes at most its one current terminal result");
    pendingJob->cancel();pendingJob.reset();
    check(!mc::telemetryGate().tryObservation()&&mc::deviceOperations().busy(),"cancel and closing INFO must not release a live fake BIOS child");
    fakeChildExit();
    check(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy()&&terminalResults==pendingBefore+1,
        "fake child exit releases ownership without a second INFO result");

    mode=Mode::PendingUntilCancelled;pendingStarted=false;
    mc::InfoOperationRequest racingInfo;racingInfo.operation=mc::InfoResourceOperation::BiosDump;
    const auto racingBefore=terminalResults.load();
    auto racingJob=mc::InfoOperation::start(std::move(racingInfo));
    const auto racingDeadline=std::chrono::steady_clock::now()+5s;
    while(!pendingStarted&&std::chrono::steady_clock::now()<racingDeadline)std::this_thread::yield();
    if(!pendingStarted)racingJob->cancel();
    check(pendingStarted&&!racingJob->done(),"fake BIOS reader did not enter before cancellation");
    racingJob->cancel();
    const auto cancelDeadline=std::chrono::steady_clock::now()+5s;
    while(!racingJob->done()&&std::chrono::steady_clock::now()<cancelDeadline)std::this_thread::yield();
    const auto* racingResult=racingJob->poll();
    check(racingJob->done()&&racingResult&&racingResult->cancelled&&terminalResults==racingBefore+1,
        "cancellation after reader start must publish one cancelled result");
    racingJob.reset();
    check(!mc::telemetryGate().tryObservation()&&mc::deviceOperations().busy(),"cancelled fake child must retain pause after INFO closes");
    fakeChildExit();
    check(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy()&&terminalResults==racingBefore+1,
        "fake child exit after cancellation must release owners without duplicate result");

    // EC diagnostics retain their own explicit sensor collection when there is
    // no firmware capture. The collector and sample source are both fake.
    unsigned ecCalls{},sampleCalls{};
    mc::InfoDumpResourceServices services;
    services.ec=[&](const auto&){mc::DiagnosticDumpResult out;auto diagnostic=mc::telemetryGate().tryDiagnosticCollection();
        if(!diagnostic){out.message="synthetic firmware capture active";return out;}
        ++ecCalls;auto sample=mc::telemetryGate().tryObservation();if(sample)++sampleCalls;out.saved=bool(sample);return out;};
    request.kind=mc::InfoDumpKind::Ec;
    std::stop_source firmwareStop;
    auto firmware=mc::telemetryGate().beginFirmwareCapture(firmwareStop.get_token(),1s);
    const auto ecWhileFirmware=mc::executeInfoDumpResource(request,&services).report;
    check(!ecWhileFirmware.saved&&ecCalls==0&&sampleCalls==0,"fake EC owner refuses its reader during active firmware capture");
    firmware.reset();
    const auto ec=mc::executeInfoDumpResource(request,&services).report;
    check(ec.saved&&ecCalls==1&&sampleCalls==1,"ordinary fake EC diagnostic must still collect its own telemetry");

    mode=Mode::Success;
    mc::InfoOperationRequest info;info.operation=mc::InfoResourceOperation::BiosDump;
    const auto before=terminalResults.load();
    auto job=mc::InfoOperation::start(std::move(info));
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(!job->done()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    check(job->done(),"real INFO lifecycle did not complete fake BIOS");
    const auto* result=job->poll();
    check(result&&result->dump.saved&&result->success&&terminalResults==before+1,"real INFO publishes one BIOS terminal result");
    check(bool(mc::telemetryGate().tryObservation())&&!mc::deviceOperations().busy(),"INFO fake BIOS leaves no ownership behind");
    std::cout<<"PASS real BIOS/INFO core: drain, safety, cancel/error, fake pending child and fake EC telemetry\n";
    return 0;
}catch(const std::exception& error){fakeChildExit();std::cerr<<"FAIL BIOS pause: "<<error.what()<<'\n';return 1;}}
