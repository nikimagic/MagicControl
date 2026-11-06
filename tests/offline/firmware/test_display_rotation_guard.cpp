// Production cross-platform rotation owner with a synthetic display action.
// No display configuration, GUI, driver, EC, firmware or OS device IO.
#include "MagicControl/DisplayRotationControl.h"
#include "MagicControl/Logging.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace mc {
void recordLog(std::string_view,std::string_view)noexcept{}
void logMessage(std::string_view,std::string_view)noexcept{}
}
namespace {
using namespace std::chrono_literals;
void check(bool ok,const char* detail){if(!ok)throw std::runtime_error(detail);}
std::uint64_t now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Signal {
    std::mutex gate;std::condition_variable changed;bool ready{};
    void notify(){std::lock_guard lock(gate);ready=true;changed.notify_one();}
    void wait(){std::unique_lock lock(gate);check(changed.wait_for(lock,3s,[&]{return ready;}),"rotation fixture readiness timeout");ready=false;}
};
struct HeldPool {
    std::mutex gate;std::condition_variable changed;unsigned entered{};bool released{};
    void block(){std::unique_lock lock(gate);++entered;changed.notify_all();changed.wait(lock,[&]{return released;});}
    bool enteredCount(unsigned value){std::unique_lock lock(gate);return changed.wait_for(lock,5s,[&]{return entered>=value;});}
    void unblock(){std::lock_guard lock(gate);released=true;changed.notify_all();}
};
struct ReleasePool {std::shared_ptr<HeldPool> pool;~ReleasePool(){pool->unblock();}};
}
int main(){try{
    mc::setLoggingEnabled(false);auto blockers=std::make_shared<HeldPool>();ReleasePool initialRelease{blockers};
    for(int i=0;i<3;++i)mc::submitDetachedResource([blockers]{blockers->block();},mc::ResourceWorkClass::Observation);
    check(blockers->enteredCount(3),"fixture did not occupy observation workers");
    mc::submitDetachedResource([blockers]{blockers->block();},mc::ResourceWorkClass::Interactive);
    check(blockers->enteredCount(4),"fixture did not occupy interactive worker");

    Signal signal;std::atomic<unsigned> writes{},begins{};
    mc::DisplayRotationOperations owner([&]{++writes;return std::string("synthetic display confirmed");},
        [&](std::uint64_t){return static_cast<std::uint64_t>(++begins);},[&]{signal.notify();});
    ReleasePool ownerRelease{blockers};
    const auto before=now();owner.request(before);
    auto ticket=mc::telemetryGate().requestFirmwareCapture();
    blockers->unblock();signal.wait();const auto queued=owner.take();
    check(queued&&!queued->success&&queued->token==1&&writes==0,"rotation queued before firmware must not execute after admission closes");
    check(!mc::telemetryGate().tryObservation(),"queued rotation must not release firmware ticket");
    check(!mc::admitOemEvent(now()),"queued OEM message must not be admitted during firmware pause");

    owner.request(now());signal.wait();const auto during=owner.take();
    check(during&&!during->success&&during->token==2&&writes==0,"rotation requested during firmware must finish rejected without a display write");
    ticket.reset();
    check(!mc::admitOemEvent(before),"OEM message queued before capture must not be admitted after it");
    owner.request(before);signal.wait();const auto stale=owner.take();
    check(stale&&!stale->success&&stale->token==3&&writes==0,"WM event timestamped before firmware must not replay after it");

    auto observation=mc::telemetryGate().tryObservation();check(bool(observation),"gate did not reopen");
    const auto boundary=observation->lastFirmwareEndMs();observation.reset();
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while(boundary&&now()<=*boundary&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    check(!boundary||now()>*boundary,"fixture could not get a fresh event timestamp");
    check(bool(mc::admitOemEvent(now())),"new OEM message must be admitted after firmware pause");
    owner.request(now());signal.wait();const auto fresh=owner.take();
    check(fresh&&fresh->success&&fresh->token==4&&writes==1,"fresh rotation after firmware must execute and confirm once");
    std::cout<<"PASS rotation owner: queued/pause/stale reject, token completion and fresh resume\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL rotation guard: "<<error.what()<<'\n';return 1;}}
