// Production KeysOperations + real profile catalog with in-memory services.
// Never opens keyboard/EC/OS adapters or real application settings.
#include "MagicControl/KeyResourceControl.h"
#include "MagicControl/BoardDescriptor.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/Logging.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace mc {
std::filesystem::path portableSettingsPath(std::string_view){throw std::runtime_error("real settings forbidden in fixture");}
std::filesystem::path settingsReadPath(const std::filesystem::path&){throw std::runtime_error("real settings forbidden in fixture");}
bool savePortableText(const std::filesystem::path&,std::string_view,std::string&){throw std::runtime_error("real settings forbidden in fixture");}
void recordLog(std::string_view,std::string_view)noexcept{}
void logMessage(std::string_view,std::string_view)noexcept{}
}
namespace {
using namespace std::chrono_literals;
void check(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
struct Signal {
    std::mutex gate;std::condition_variable changed;bool ready{};
    void notify(){std::lock_guard lock(gate);ready=true;changed.notify_one();}
    void wait(){std::unique_lock lock(gate);check(changed.wait_for(lock,3s,[&]{return ready;}),"KEYs fixture readiness timeout");ready=false;}
};
}
int main(){try{
    mc::setLoggingEnabled(false);mc::setBatteryLoggingEnabled(false);
    std::vector<std::string> profiles;
    for(const auto* profile:mc::deviceProfiles())if(profile)profiles.emplace_back(profile->id);
    check(!profiles.empty(),"device catalog is empty");profiles.emplace_back(); // Unknown
    unsigned combinations{};
    for(const auto& profile:profiles)for(const bool linuxHost:{false,true}){
        Signal signal;std::atomic<unsigned> reads{},writes{};
        mc::KeyOperationServices fake;
        fake.localState=[]{return false;};
        fake.read=[&]{++reads;mc::KeySnapshot state;state.available.fill(true);state.value.fill(1);return state;};
        fake.execute=[&](const mc::KeyCommand&,mc::KeySnapshot state,const std::function<void()>&){
            ++writes;mc::KeyBackendResult result;result.snapshot=std::move(state);result.success=true;return result;};
        mc::KeysOperations owner(profile,false,linuxHost,std::move(fake),[&]{signal.notify();});
        check(owner.refresh(),"KEYs initial fake refresh not scheduled");signal.wait();
        check(owner.take().has_value()&&reads==1,"KEYs initial fake observation not published");
        const auto baseline=owner.snapshot().value;

        std::stop_source stop;auto pause=mc::telemetryGate().beginFirmwareCapture(stop.get_token(),1s);
        check(bool(pause)&&owner.refresh(),"KEYs fake refresh not queued during firmware pause");signal.wait();
        check(!owner.take().has_value()&&reads==1&&writes==0&&owner.snapshot().value==baseline,
            "KEYs must skip a paused observation and preserve the last published state");
        pause.reset();
        check(owner.refresh(),"KEYs did not reschedule after firmware completion");signal.wait();
        check(owner.take().has_value()&&reads==2&&writes==0,"KEYs fake observation did not resume");
        ++combinations;
    }
    std::cout<<"PASS real KEYs owner: "<<profiles.size()<<" actual profile IDs including Unknown, "<<combinations<<" Windows/Linux policy combinations\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL KEYs telemetry: "<<error.what()<<'\n';return 1;}}
