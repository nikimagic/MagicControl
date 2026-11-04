#include "MagicControl/ResourceControl.h"
#include "MagicControl/LiquidCooling.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/Logging.h"
#include "../../protocols/uniwill/hydroc16g2/LiquidCooling.h"
#include <chrono>
#include <condition_variable>
#include <thread>
#include <mutex>
#include <atomic>
#include <stdexcept>
namespace mc {
namespace {
using Clock=std::chrono::steady_clock;
std::uint64_t nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
void cancellation(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("LPP operation cancelled; accepted commands are not replayed");}
}
struct LiquidCoolingController::Impl {
    std::unique_ptr<ILiquidCoolingBackend> backend;std::mutex gate;std::condition_variable wake;
    LiquidCoolingState cached;bool closing{};std::atomic<bool> suspendRequested{};Clock::time_point next{},nextMeter{};
    std::uint64_t pumpStartedAt{};ResourceActor worker;
    explicit Impl(std::unique_ptr<ILiquidCoolingBackend> value):backend(std::move(value)){
        if(!backend)throw std::invalid_argument("LPP: missing platform backend");worker=ResourceActor([this]{run();});
    }
    ~Impl(){ {std::lock_guard lock(gate);closing=true;}wake.notify_all();if(worker.joinable())worker.join(); }
    LiquidCoolingLink ready(){
        if(cached.suspended||suspendRequested.load())throw std::runtime_error("LPP suspended/power epoch changed; explicit connect required");
        const auto link=backend->link();if(!link.connected||!link.notificationsReady||link.firmware.empty())throw std::runtime_error("LPP: transport/notifications/firmware handshake not ready");return link;
    }
    void status(std::string message){cached.status=std::move(message);logMessage("liquid_cooling",cached.status);}
    void disarm(){
        cached.automatic=false;next={};
        try{backend->releaseNativePolicy();cached.nativePolicyOwned=backend->nativePolicyOwned();}
        catch(const std::exception& e){cached.commandUncertain=true;cached.nativePolicyOwned=backend->nativePolicyOwned();status(std::string("LPP native policy release failed/uncertain: ")+e.what());throw;}
    }
    void writePump(const LiquidPumpCommand& c,std::stop_token stop){
        cancellation(stop);ready();const auto packet=hydroc16g2::lpp::pump(c);
        try{backend->write(packet,stop);if(c.enabled&&(!cached.acceptedPump||!cached.acceptedPump->enabled))pumpStartedAt=nowMs();cached.acceptedPump=c;cached.commandUncertain=false;}
        catch(const std::exception& e){cached.commandUncertain=true;cached.acceptedPump.reset();status(std::string("LPP pump command failed/uncertain: ")+e.what());throw;}
    }
    void writeFan(const LiquidFanCommand& c,std::stop_token stop){
        cancellation(stop);ready();const auto packet=hydroc16g2::lpp::fan(c);
        try{backend->write(packet,stop);cached.acceptedFan=c;cached.commandUncertain=false;}
        catch(const std::exception& e){cached.commandUncertain=true;cached.acceptedFan.reset();status(std::string("LPP fan command failed/uncertain: ")+e.what());throw;}
    }
    bool freshFlow(const LiquidCoolingLink& link){return link.meterState==2&&nowMs()>=link.meterAtMs&&nowMs()-link.meterAtMs<=5000;}
    void doSuspend()noexcept{
        cached.automatic=false;cached.suspended=true;cached.commandUncertain=true;
        try{backend->releaseNativePolicy();status("LPP suspended: own native LC policy released; no automatic reconnect/restart; external outputs unmeasured");}
        catch(const std::exception& e){status(std::string("LPP suspend policy cleanup failed: ")+e.what());}
        cached.nativePolicyOwned=backend->nativePolicyOwned();backend->close();
    }
    void abortAutomatic(std::string reason){
        cached.automatic=false;cached.commandUncertain=true;
        try{backend->releaseNativePolicy();reason+="; own native LC policy released";}
        catch(const std::exception& e){reason+="; native policy cleanup failed/uncertain: "+std::string(e.what());}
        cached.nativePolicyOwned=backend->nativePolicyOwned();status(std::move(reason));
    }
    void tick(){
        const auto link=backend->link();
        // Critical cleanup is independent of ordinary UI/background admission.
        // The typed EC controller still serializes the actual masked RMW.
        if(!link.connected){abortAutomatic("LPP BLE lost; physical pump/fan state unknown; no reconnect");return;}
        if(cached.acceptedPump&&cached.acceptedPump->enabled&&nowMs()-pumpStartedAt>3000&&
            (!link.meterState||link.meterAtMs>nowMs()||nowMs()-link.meterAtMs>6000||*link.meterState!=2)){
            abortAutomatic("LPP flow abnormal/stale; Auto stopped without replaying output commands");return;
        }
        const auto reservation=deviceOperations().tryBegin(DeviceOperationKind::Background);if(!reservation)return;
        try{
            if(Clock::now()>=nextMeter){backend->write(hydroc16g2::lpp::meter,{});nextMeter=Clock::now()+std::chrono::seconds(2);}
            if(Clock::now()<next)return;
            const auto target=backend->automaticTargets();
            // A fresh abnormal notification is decisive. An unavailable flow
            // observation never becomes "normal" by default.
            const auto observed=backend->link();
            if(target.pump.enabled&&observed.meterState&&observed.meterAtMs>0&&nowMs()-observed.meterAtMs<=5000&&*observed.meterState!=2)
                throw std::runtime_error("LPP: fresh flow-meter state is abnormal; native automatic policy released");
            if(!cached.acceptedPump||*cached.acceptedPump!=target.pump)writePump(target.pump,{});
            if(!cached.acceptedFan||*cached.acceptedFan!=target.fan)writeFan(target.fan,{});
            next=Clock::now()+std::chrono::seconds(20);
            status("LPP Auto: native EC targets relayed over BLE; GATT acceptance only, physical RPM/duty unmeasured");
        }catch(const std::exception& e){abortAutomatic("LPP Auto stopped without command replay: "+std::string(e.what()));}
    }
    void run(){
        std::unique_lock lock(gate);
        while(!closing){
            wake.wait_for(lock,std::chrono::milliseconds(250));
            if(closing)break;
            if(suspendRequested.exchange(false)){doSuspend();continue;}
            if(cached.automatic)try{tick();}catch(const std::exception& e){cached.automatic=false;cached.commandUncertain=true;status(std::string("LPP background error: ")+e.what());}
        }
        doSuspend();
    }
};
LiquidCoolingController::LiquidCoolingController(std::unique_ptr<ILiquidCoolingBackend> backend):impl_(std::make_unique<Impl>(std::move(backend))){}
LiquidCoolingController::~LiquidCoolingController()=default;
std::vector<LiquidCoolingDevice> LiquidCoolingController::discover(std::stop_token stop){std::lock_guard lock(impl_->gate);cancellation(stop);try{return impl_->backend->discover(stop);}catch(const std::exception& e){impl_->status(std::string("LPP discovery failed: ")+e.what());throw;}}
void LiquidCoolingController::connect(const std::string& id,std::stop_token stop){
    std::lock_guard lock(impl_->gate);cancellation(stop);impl_->disarm();impl_->backend->close();impl_->cached={};impl_->suspendRequested.store(false);
    try{impl_->backend->connect(id,stop);impl_->ready();impl_->status("LPP connected and firmware identified; no pump/fan start commands sent by connect");}
    catch(const std::exception& e){impl_->backend->close();impl_->cached.commandUncertain=true;impl_->status(std::string("LPP connection failed: ")+e.what());throw;}
}
LiquidCoolingState LiquidCoolingController::state(){
    std::lock_guard lock(impl_->gate);auto state=impl_->cached;state.link=impl_->backend->link();state.nativePolicyOwned=impl_->backend->nativePolicyOwned();
    if(impl_->suspendRequested.load())state.suspended=true;
    if(!state.link.connected&&(state.acceptedPump||state.acceptedFan))state.commandUncertain=true;return state;
}
std::optional<LiquidCoolingState> LiquidCoolingController::tryState(){
    std::unique_lock lock(impl_->gate,std::try_to_lock);if(!lock.owns_lock())return {};
    auto state=impl_->cached;state.link=impl_->backend->link();state.nativePolicyOwned=impl_->backend->nativePolicyOwned();
    if(impl_->suspendRequested.load())state.suspended=true;
    if(!state.link.connected&&(state.acceptedPump||state.acceptedFan))state.commandUncertain=true;return state;
}
void LiquidCoolingController::setPump(const LiquidPumpCommand& c,std::stop_token stop){
    hydroc16g2::lpp::pump(c);std::lock_guard lock(impl_->gate);impl_->disarm();impl_->writePump(c,stop);
    impl_->status("LPP manual pump command accepted by GATT; flow and physical pump speed remain separate observations");
}
void LiquidCoolingController::setFan(const LiquidFanCommand& c,std::stop_token stop){
    hydroc16g2::lpp::fan(c);std::lock_guard lock(impl_->gate);impl_->disarm();impl_->writeFan(c,stop);
    impl_->status("LPP external radiator-fan command accepted by GATT; laptop CPU/GPU fan tables untouched");
}
void LiquidCoolingController::requestMeter(std::stop_token stop){std::lock_guard lock(impl_->gate);impl_->ready();impl_->backend->write(hydroc16g2::lpp::meter,stop);}
void LiquidCoolingController::enableAutomatic(std::stop_token stop){
    std::lock_guard lock(impl_->gate);impl_->ready();if(impl_->cached.automatic)return;
    impl_->disarm();impl_->backend->prepareAutomatic();
    // Explicit request only. Native OEM default provides flow before allowing
    // firmware to select a liquid-cooling power/thermal policy.
    impl_->writePump({true,60,PumpVoltage::V8},stop);impl_->writeFan({true,50},stop);
    const auto since=nowMs();impl_->backend->write(hydroc16g2::lpp::meter,stop);
    const auto deadline=Clock::now()+std::chrono::seconds(3);
    while(Clock::now()<deadline){cancellation(stop);const auto link=impl_->ready();
        if(impl_->freshFlow(link)&&link.meterAtMs>=since)break;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    const auto link=impl_->ready();if(!impl_->freshFlow(link)||link.meterAtMs<since){
        impl_->status("LPP Auto not enabled: fresh normal flow unavailable; explicit default pump/fan commands accepted, native LC policy unchanged");
        throw std::runtime_error(impl_->cached.status);
    }
    try{impl_->backend->acquireNativePolicy();}catch(const std::exception& e){impl_->cached.commandUncertain=true;impl_->cached.nativePolicyOwned=impl_->backend->nativePolicyOwned();impl_->status(std::string("LPP Auto policy acquisition failed; cleanup/readback status retained: ")+e.what());throw;}
    impl_->cached.nativePolicyOwned=true;impl_->cached.automatic=true;
    impl_->next=Clock::now()+std::chrono::milliseconds(5500);impl_->nextMeter=Clock::now()+std::chrono::seconds(2);impl_->wake.notify_all();
    impl_->status("LPP Auto armed after fresh normal flow; waiting for native EC targets; no CPU PL/FAN-table writes");
}
void LiquidCoolingController::disableAutomatic(){std::lock_guard lock(impl_->gate);impl_->disarm();impl_->status("LPP Auto disabled; own LC policy released; last external outputs retained as accepted intent only");}
void LiquidCoolingController::stopCooling(std::stop_token stop){
    std::lock_guard lock(impl_->gate);impl_->disarm();impl_->writePump({false,0,PumpVoltage::V8},stop);impl_->writeFan({false,0},stop);
    impl_->status("LPP pump/fan off commands accepted; physical stop is not inferred from BLE disconnection");
}
void LiquidCoolingController::disconnect(bool stopFirst,std::stop_token stop){
    std::lock_guard lock(impl_->gate);impl_->disarm();
    if(stopFirst){impl_->writePump({false,0,PumpVoltage::V8},stop);impl_->writeFan({false,0},stop);impl_->backend->write(hydroc16g2::lpp::lineOff,stop);}
    impl_->backend->close();impl_->cached.commandUncertain=!stopFirst;
    impl_->status(stopFirst?"LPP off/lineoff commands accepted before closing BLE; no physical-stop measurement":"LPP BLE closed without output commands; physical pump/fan state unknown");
}
void LiquidCoolingController::suspend(){impl_->suspendRequested.store(true);impl_->wake.notify_all();}
void LiquidCoolingController::sleep(std::stop_token stop){
    std::lock_guard lock(impl_->gate);impl_->disarm();impl_->ready();impl_->backend->write(hydroc16g2::lpp::sleep,stop);
    impl_->backend->close();impl_->cached.suspended=true;impl_->cached.commandUncertain=true;
    impl_->status("LPP native sleep command accepted, BLE closed; physical outputs unmeasured; explicit reconnect required");
}
}
