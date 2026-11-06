// Real Linux FANs observation owner and real profile/resource projections.
// The MonitoringReader is link-time fake; monitoringSource is in-memory only.
// Never opens sysfs, GPU libraries, EC, drivers or application settings.
#include "FansServicesLinux.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace mc {
struct MonitoringReader::Impl {};
MonitoringReader::MonitoringReader(EcAccess*,bool,bool):impl_(std::make_unique<Impl>()){}
MonitoringReader::~MonitoringReader()=default;
MonitoringSnapshot MonitoringReader::read(){throw std::runtime_error("fake FANs source unexpectedly used MonitoringReader");}
void recordLog(std::string_view,std::string_view)noexcept{}
void logMessage(std::string_view,std::string_view)noexcept{}
}
namespace {
using namespace std::chrono_literals;
void check(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
struct Signal {
    std::mutex gate;std::condition_variable changed;bool ready{};
    void notify(){std::lock_guard lock(gate);ready=true;changed.notify_one();}
    void wait(){std::unique_lock lock(gate);check(changed.wait_for(lock,3s,[&]{return ready;}),"FANs fixture readiness timeout");ready=false;}
};
mc::BoardDescriptor boardFor(const mc::DeviceProfile& profile){
    return {std::string(profile.manufacturer),std::string(profile.baseBoard),{},std::string(profile.productName)};
}
}
int main(){try{
    mc::setLoggingEnabled(false);mc::setBatteryLoggingEnabled(false);
    std::vector<mc::BoardDescriptor> boards;
    for(const auto* profile:mc::deviceProfiles())if(profile)boards.push_back(boardFor(*profile));
    check(!boards.empty(),"device catalog is empty");
    boards.push_back({"Fake Unknown Manufacturer","Fake Unknown Board",{},"Fake Unknown Product"});
    check(mc::deviceProfile(boards.back()).id.empty(),"unknown fixture matched a real profile");
    for(const auto& board:boards){
        Signal signal;std::atomic<unsigned> reads{};
        auto source=[&]{mc::MonitoringSnapshot sample;sample.processors[0].celsius=40.+double(++reads);
            sample.processors[0].temperatureSource="in-memory fixture";sample.processorSampledAtMs=mc::batteryMonotonicMs();return sample;};
        mc::linux_platform::FansOperationsLinux owner(board,false,source);
        check(owner.poll([&]{signal.notify();}),"FANs initial fake refresh not scheduled");signal.wait();
        check(owner.take().has_value()&&reads==1&&owner.presentation().monitoring.processors[0].celsius==41.,"FANs initial observation missing");

        std::stop_source stop;auto pause=mc::telemetryGate().beginFirmwareCapture(stop.get_token(),1s);
        check(bool(pause)&&owner.poll([&]{signal.notify();}),"FANs fake refresh not scheduled while paused");signal.wait();
        check(!owner.take().has_value()&&reads==1&&owner.presentation().monitoring.processors[0].celsius==41.,
            "FANs paused refresh must skip provider and retain prior display");
        pause.reset();
        check(owner.poll([&]{signal.notify();}),"FANs fake refresh not rescheduled");signal.wait();
        check(owner.take().has_value()&&reads==2&&owner.presentation().monitoring.processors[0].celsius==42.,"FANs fake refresh did not resume");
    }

    // The real owner holds its admitted sample until its fake source returns.
    // A firmware request must close the gate first and wait for this sample.
    Signal signal;std::promise<void> entered,release;auto enteredFuture=entered.get_future();auto allowed=release.get_future().share();
    std::atomic<unsigned> reads{};
    auto source=[&]{mc::MonitoringSnapshot sample;const auto count=++reads;
        if(count==1){entered.set_value();allowed.wait();}sample.processors[0].celsius=50.+count;return sample;};
    mc::linux_platform::FansOperationsLinux owner(boards.back(),false,source);
    struct Unblock {std::promise<void>& signal;bool sent{};void now(){if(!sent){sent=true;signal.set_value();}}~Unblock(){try{now();}catch(...){}}} unblock{release};
    check(owner.poll([&]{signal.notify();})&&enteredFuture.wait_for(3s)==std::future_status::ready,"in-flight FANs fake source did not start");
    std::stop_source stop;auto pending=std::async(std::launch::async,[&]{return mc::telemetryGate().beginFirmwareCapture(stop.get_token(),5s);});
    const auto deadline=std::chrono::steady_clock::now()+3s;bool paused{};
    do{if(!mc::telemetryGate().tryObservation()){paused=true;break;}std::this_thread::yield();}while(std::chrono::steady_clock::now()<deadline);
    const bool waiting=pending.wait_for(0ms)==std::future_status::timeout;
    unblock.now();signal.wait();check(owner.take().has_value(),"admitted in-flight FANs sample did not complete");
    const bool drained=pending.wait_for(3s)==std::future_status::ready;if(!drained)stop.request_stop();
    auto pause=pending.get();check(paused&&waiting&&drained&&pause&&reads==1,"firmware did not drain real FANs owner before capture");
    check(owner.poll([&]{signal.notify();}),"paused FANs skip not scheduled");signal.wait();
    check(!owner.take().has_value()&&reads==1,"real FANs owner called provider while firmware capture active");pause.reset();

    std::cout<<"PASS real Linux FANs owner with fake source across "<<boards.size()<<" catalog/Unknown boards\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL Linux FANs telemetry: "<<error.what()<<'\n';return 1;}}
