// Production GPU presence owner with injected, memory-only device readers.
// No adapter enumeration, driver, GPU, EC, firmware or user files.
#include "MagicControl/GpuPresence.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
void check(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
bool waitUntilPaused(mc::TelemetryGate& gate){
    const auto deadline=std::chrono::steady_clock::now()+3s;
    do{if(!gate.tryObservation())return true;std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);
    return false;
}
}
int main(){try{
    mc::TelemetryGate gate;mc::GpuPresence presence(gate);
    std::atomic<unsigned> nvidiaCalls{},radeonCalls{},vbiosCalls{};
    for(const auto kind:{mc::GpuPresenceKind::Nvidia,mc::GpuPresenceKind::Radeon,mc::GpuPresenceKind::VbiosDump}){
        unsigned readerCalls{};
        const bool found=mc::observeGpuPresence([]()->mc::GpuPresence&{throw std::bad_alloc();},kind,[&]{++readerCalls;return true;});
        check(!found&&readerCalls==0,"public presence helper must catch owner allocation failure before calling the reader");
    }
    auto nvidia=[&]{++nvidiaCalls;return true;};auto radeon=[&]{++radeonCalls;return false;};
    check(presence.observe(mc::GpuPresenceKind::Nvidia,nvidia),"initial fake NVIDIA presence missing");
    check(!presence.observe(mc::GpuPresenceKind::Radeon,radeon),"initial fake Radeon absence missing");
    check(!presence.observe(mc::GpuPresenceKind::VbiosDump,[&]{++vbiosCalls;return false;}),"initial fake VBIOS availability missing");

    std::stop_source stop;auto pause=gate.beginFirmwareCapture(stop.get_token(),1s);
    const bool keptNvidia=presence.observe(mc::GpuPresenceKind::Nvidia,[&]{++nvidiaCalls;return false;});
    const bool keptRadeon=presence.observe(mc::GpuPresenceKind::Radeon,[&]{++radeonCalls;return true;});
    const bool keptVbios=presence.observe(mc::GpuPresenceKind::VbiosDump,[&]{++vbiosCalls;return true;});
    check(keptNvidia&&!keptRadeon&&!keptVbios&&nvidiaCalls==1&&radeonCalls==1&&vbiosCalls==1,"paused discovery must retain each independent last-known value with zero reader calls");
    mc::GpuPresence pristine(gate);unsigned firstCalls{};
    check(!pristine.observe(mc::GpuPresenceKind::Nvidia,[&]{++firstCalls;return true;})&&
          !pristine.observe(mc::GpuPresenceKind::Radeon,[&]{++firstCalls;return true;})&&
          !pristine.observe(mc::GpuPresenceKind::VbiosDump,[&]{++firstCalls;return true;})&&firstCalls==0,
        "paused discovery without a previous sample returns its default without querying any reader");
    pause.reset();
    check(!presence.observe(mc::GpuPresenceKind::Nvidia,[&]{++nvidiaCalls;return false;}),"NVIDIA presence did not refresh after pause");
    check(presence.observe(mc::GpuPresenceKind::Radeon,[&]{++radeonCalls;return true;}),"Radeon presence did not refresh after pause");
    check(presence.observe(mc::GpuPresenceKind::VbiosDump,[&]{++vbiosCalls;return true;}),"VBIOS availability did not refresh after pause");
    check(nvidiaCalls==2&&radeonCalls==2&&vbiosCalls==2,"all independent readers must resume after pause");

    std::promise<void> entered,release;auto enteredFuture=entered.get_future();auto allowed=release.get_future().share();
    auto inFlight=std::async(std::launch::async,[&]{return presence.observe(mc::GpuPresenceKind::Nvidia,[&]{++nvidiaCalls;entered.set_value();allowed.wait();return true;});});
    struct Unblock {std::promise<void>& signal;bool sent{};void now(){if(!sent){sent=true;signal.set_value();}}~Unblock(){try{now();}catch(...){}}} unblock{release};
    check(enteredFuture.wait_for(3s)==std::future_status::ready,"fake in-flight presence reader did not start");
    auto capture=std::async(std::launch::async,[&]{return gate.beginFirmwareCapture(stop.get_token(),5s);});
    const bool denied=waitUntilPaused(gate),waiting=capture.wait_for(0ms)==std::future_status::timeout;
    const auto callsWhileActive=nvidiaCalls.load();
    const bool cached=presence.observe(mc::GpuPresenceKind::Nvidia,[&]{++nvidiaCalls;return true;});
    const bool noCall=nvidiaCalls==callsWhileActive;
    unblock.now();const bool oldCompleted=inFlight.get();
    const bool drained=capture.wait_for(3s)==std::future_status::ready;if(!drained)stop.request_stop();
    auto ticket=capture.get();
    check(denied&&waiting&&!cached&&noCall&&oldCompleted&&drained&&ticket,"firmware must await admitted discovery and block new discovery");
    check(presence.observe(mc::GpuPresenceKind::Nvidia,[&]{++nvidiaCalls;return false;})&&nvidiaCalls==callsWhileActive,
        "finished in-flight discovery must be retained without a new query during capture");
    ticket.reset();
    check(!presence.observe(mc::GpuPresenceKind::Nvidia,[&]{++nvidiaCalls;return false;})&&nvidiaCalls==callsWhileActive+1,
        "NVIDIA discovery must resume after capture");
    std::cout<<"PASS GPU presence owner: Nvidia/Radeon/VBIOS cache, in-flight drain, zero paused calls and resume\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL GPU presence: "<<error.what()<<'\n';return 1;}}
