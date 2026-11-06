#pragma once
#include <atomic>
#include <memory>
#include <functional>
#include <mutex>
#include "ActionLog.h"
#include "TelemetryGate.h"

namespace mc {
enum class DeviceOperationKind { Interactive, Background };
class DeviceOperationGate;
class DeviceOperation {
public:
    ~DeviceOperation();
    DeviceOperation(const DeviceOperation&) = delete;
    DeviceOperation& operator=(const DeviceOperation&) = delete;
    audit::Id actionId()const noexcept{return actionId_;}
private:
    friend class DeviceOperationGate;
    explicit DeviceOperation(DeviceOperationGate& gate,bool interactive,std::optional<TelemetryGate::Observation> observation)
        : gate_(&gate),interactive_(interactive),actionId_(interactive?audit::capture():0),observation_(std::move(observation)) {}
    DeviceOperationGate* gate_;
    bool interactive_;
    audit::Id actionId_{};
    std::optional<TelemetryGate::Observation> observation_;
};
class DeviceOperationGate {
public:
    // Dump-only scheduling hint for optional EC getters. It never grants
    // write ownership, blocks commands or suspends an online cooling loop.
    class PassiveEcReadPause {
    public:
        explicit PassiveEcReadPause(DeviceOperationGate& gate, bool enabled) noexcept
            : gate_(enabled ? &gate : nullptr) { if(gate_)gate_->passivePauses_.fetch_add(1); }
        ~PassiveEcReadPause() { if(gate_)gate_->passivePauses_.fetch_sub(1); }
        PassiveEcReadPause(const PassiveEcReadPause&)=delete;
        PassiveEcReadPause& operator=(const PassiveEcReadPause&)=delete;
    private:
        DeviceOperationGate* gate_;
    };
    bool passiveEcReadsPaused() const noexcept { return passivePauses_.load()!=0; }
    std::shared_ptr<DeviceOperation> tryBegin(DeviceOperationKind kind=DeviceOperationKind::Interactive) {
        const bool interactive=kind==DeviceOperationKind::Interactive;
        std::optional<TelemetryGate::Observation> observation;
        observation=telemetryGate().tryObservation();if(!observation)return {};
        auto free = State::Idle;
        if (!state_.compare_exchange_strong(free,interactive?State::Interactive:State::Background)){if(interactive&&audit::capture())audit::related(audit::capture(),"operation_rejected","\"reason\":\"device_operation_busy\"");return {};}
        try { auto operation = std::shared_ptr<DeviceOperation>(new DeviceOperation(*this,interactive,std::move(observation))); if(interactive)notify(); return operation; }
        catch (...) { state_.store(State::Idle); throw; }
    }
    // A firmware ticket is the only route allowed to reserve the device
    // while observations are closed; it must belong to the process gate and
    // have completed the drain before any hardware collector is started.
    std::shared_ptr<DeviceOperation> tryBeginFirmware(const std::shared_ptr<TelemetryGate::FirmwarePause>& pause){
        if(!telemetryGate().owns(pause)||!pause->quiescent())return {};
        auto free=State::Idle;
        if(!state_.compare_exchange_strong(free,State::Interactive))return {};
        try{auto operation=std::shared_ptr<DeviceOperation>(new DeviceOperation(*this,true,{}));notify();return operation;}
        catch(...){state_.store(State::Idle);throw;}
    }
    bool busy() const noexcept { return state_.load()!=State::Idle; }
    // UI availability reflects user commands, not brief periodic RGB writes.
    // Both kinds still exclude other operations through the SAME atomic gate.
    bool interactiveBusy() const noexcept { return state_.load()==State::Interactive; }
    bool owns(const std::shared_ptr<DeviceOperation>& operation) const noexcept {
        return operation && operation->gate_ == this;
    }
    void setNotifier(std::function<void()> notifier) { std::lock_guard lock(mutex_); notifier_ = std::move(notifier); }
private:
    friend class DeviceOperation;
    enum class State { Idle, Interactive, Background };
    std::atomic<State> state_{State::Idle};
    std::atomic<unsigned> passivePauses_{};
    std::mutex mutex_;
    std::function<void()> notifier_;
    void notify() noexcept {
        try { std::function<void()> call; { std::lock_guard lock(mutex_); call = notifier_; } if (call) call(); } catch (...) {}
    }
};
inline DeviceOperation::~DeviceOperation() { gate_->state_.store(DeviceOperationGate::State::Idle); if(interactive_)gate_->notify(); }
inline DeviceOperationGate& deviceOperations() { static DeviceOperationGate gate; return gate; }
} // namespace mc
