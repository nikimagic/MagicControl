// Production shared GPU owner with fake provider. No GPU library or device IO.
#include "DiscreteGpuOperation.h"
#include "MagicControl/Logging.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {
std::atomic<unsigned> reads{},writes{};
void check(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
struct Signal {
    std::mutex gate;std::condition_variable changed;bool ready{};
    void notify(){std::lock_guard lock(gate);ready=true;changed.notify_one();}
    void wait(){std::unique_lock lock(gate);check(changed.wait_for(lock,std::chrono::seconds(3),[&]{return ready;}),"GPU fake notification timed out");ready=false;}
};
}
namespace mc {
struct DiscreteGpu::Impl {GpuVendor vendor;explicit Impl(GpuVendor value):vendor(value){}};
DiscreteGpu::DiscreteGpu(GpuVendor vendor):impl_(std::make_unique<Impl>(vendor)){}
DiscreteGpu::~DiscreteGpu()=default;
GpuVendor DiscreteGpu::vendor()const{return impl_->vendor;}
std::vector<GpuReading> DiscreteGpu::read(){
    const auto sample=++reads;GpuReading row;row.uuid=impl_->vendor==GpuVendor::Nvidia?"fake-nvidia":"fake-radeon";
    row.physicalId="fake-gpu";row.available=true;row.temperature=40+sample;return {row};
}
void DiscreteGpu::setPowerLimit(const std::string&,int){++writes;}
void DiscreteGpu::resetPowerLimit(const std::string&){++writes;}
void recordLog(std::string_view,std::string_view)noexcept{}
void logMessage(std::string_view,std::string_view)noexcept{}
}
int main(){try{
    mc::setLoggingEnabled(false);mc::setBatteryLoggingEnabled(false);
    for(const auto vendor:{mc::GpuVendor::Nvidia,mc::GpuVendor::Radeon}){
        reads=0;writes=0;Signal signal;mc::GpuOperations owner(vendor);owner.bindReady([&]{signal.notify();});
        mc::GpuOperationRequest request;
        check(owner.submit(request,false),"GPU initial fake refresh not scheduled");signal.wait();
        auto initial=owner.take();check(initial&&reads==1&&owner.readings.size()==1&&owner.readings[0].temperature==41u,"GPU initial fake reading missing");

        std::stop_source stop;auto pause=mc::telemetryGate().beginFirmwareCapture(stop.get_token(),std::chrono::seconds(1));
        check(bool(pause)&&owner.submit(request,false),"GPU fake refresh not scheduled while paused");signal.wait();
        check(!owner.take()&&reads==1&&writes==0&&owner.readings.size()==1&&owner.readings[0].temperature==41u,
            "GPU paused refresh must skip provider and retain prior display");
        pause.reset();
        check(owner.submit(request,false),"GPU fake refresh not rescheduled");signal.wait();
        auto resumed=owner.take();check(resumed&&reads==2&&writes==0&&owner.readings[0].temperature==42u,"GPU fake refresh did not resume");
    }
    std::cout<<"PASS production GPU owner: Nvidia/Radeon fake provider paused and resumed\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL GPU telemetry: "<<error.what()<<'\n';return 1;}}
