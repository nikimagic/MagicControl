#pragma once

#include "FirmwareEventTime.h"
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <utility>

namespace mc {

// A firmware capture first closes admission and then waits for admitted
// observations to finish. Control loops which rely on fresh measurements use
// a separate long-lived safety lease: a capture must not silently pause them.
// This gate is process-local; cross-process coordination needs its own owner.
class TelemetryGate {
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        unsigned observations{}, safetyControls{}, diagnosticCollections{};
        std::uint64_t firmwareGeneration{};
        std::optional<std::uint64_t> lastFirmwareEndMs;
        std::optional<FirmwareEventBoundary> lastFirmwareEventBoundary;
        bool firmwarePending{};
    };
    std::shared_ptr<State> state_{std::make_shared<State>()};

public:
    class Observation {
        friend class TelemetryGate;
        std::shared_ptr<State> state_;
        std::uint64_t generation_{};
        std::optional<std::uint64_t> lastFirmwareEndMs_;
        std::optional<FirmwareEventBoundary> eventBoundary_;
        explicit Observation(std::shared_ptr<State> state,std::uint64_t generation,std::optional<std::uint64_t> lastEnd,std::optional<FirmwareEventBoundary> boundary) noexcept : state_(std::move(state)),generation_(generation),lastFirmwareEndMs_(lastEnd),eventBoundary_(boundary) {}
        void release() noexcept {
            if (!state_) return;
            auto state=std::move(state_);
            { std::lock_guard lock(state->mutex); --state->observations; }
            state->changed.notify_all();
        }
    public:
        Observation(const Observation&)=delete;
        Observation& operator=(const Observation&)=delete;
        Observation(Observation&& other) noexcept : state_(std::move(other.state_)),generation_(other.generation_),lastFirmwareEndMs_(other.lastFirmwareEndMs_),eventBoundary_(other.eventBoundary_) {}
        Observation& operator=(Observation&& other) noexcept {
            if (this!=&other) { release(); state_=std::move(other.state_);generation_=other.generation_;lastFirmwareEndMs_=other.lastFirmwareEndMs_;eventBoundary_=other.eventBoundary_; }
            return *this;
        }
        [[nodiscard]] std::uint64_t generation()const noexcept{return generation_;}
        [[nodiscard]] std::optional<std::uint64_t> lastFirmwareEndMs()const noexcept{return lastFirmwareEndMs_;}
        [[nodiscard]] std::optional<FirmwareEventBoundary> firmwareEventBoundary()const noexcept{return eventBoundary_;}
        // receivedAt must use steady_clock milliseconds, as OEM hotkey events
        // do. Equal timestamps are conservatively treated as stale.
        [[nodiscard]] bool acceptsEvent(std::uint64_t receivedAt)const noexcept{return !lastFirmwareEndMs_||receivedAt>*lastFirmwareEndMs_;}
        ~Observation(){release();}
    };

    class SafetyControl {
        friend class TelemetryGate;
        std::shared_ptr<State> state_;
        explicit SafetyControl(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}
        void release() noexcept {
            if (!state_) return;
            auto state=std::move(state_);
            { std::lock_guard lock(state->mutex); --state->safetyControls; }
            state->changed.notify_all();
        }
    public:
        SafetyControl(const SafetyControl&)=delete;
        SafetyControl& operator=(const SafetyControl&)=delete;
        SafetyControl(SafetyControl&& other) noexcept : state_(std::move(other.state_)) {}
        SafetyControl& operator=(SafetyControl&& other) noexcept {
            if (this!=&other) { release(); state_=std::move(other.state_); }
            return *this;
        }
        ~SafetyControl(){release();}
    };

    class FirmwarePause {
        friend class TelemetryGate;
        std::shared_ptr<State> state_;
        std::atomic<bool> quiescent_{};
        FirmwareWallClock::time_point startedWall_;
        FirmwareSteadyClock::time_point startedSteady_;
    public:
        // State is private to TelemetryGate; public construction here permits
        // make_shared to allocate before the guard owns the locked state.
        explicit FirmwarePause(std::shared_ptr<State> state) noexcept : state_(std::move(state)),startedWall_(FirmwareWallClock::now()),startedSteady_(FirmwareSteadyClock::now()) {}
        FirmwarePause(const FirmwarePause&)=delete;
        FirmwarePause& operator=(const FirmwarePause&)=delete;
        void waitForQuiescence(std::stop_token token,std::chrono::milliseconds timeout){
            auto state=state_;
            std::stop_callback wake(token,[state]{
                { std::lock_guard lock(state->mutex); }
                state->changed.notify_all();
            });
            std::unique_lock lock(state->mutex);
            const auto deadline=std::chrono::steady_clock::now()+timeout;
            if(!state->changed.wait_until(lock,deadline,[&]{return token.stop_requested()||state->observations==0;}))
                throw std::runtime_error("Firmware capture unavailable: background telemetry did not finish in time");
            if(token.stop_requested())throw std::runtime_error("Firmware capture cancelled while stopping telemetry");
            quiescent_.store(true,std::memory_order_release);
        }
        [[nodiscard]] bool quiescent()const noexcept{return quiescent_.load(std::memory_order_acquire);}
        ~FirmwarePause(){
            const auto endedSteady=FirmwareSteadyClock::now();const auto endedWall=FirmwareWallClock::now();
            { std::lock_guard lock(state_->mutex);
              state_->lastFirmwareEndMs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(endedSteady.time_since_epoch()).count());
              state_->lastFirmwareEventBoundary=makeFirmwareEventBoundary(startedWall_,startedSteady_,endedWall,endedSteady);
              state_->firmwarePending=false; }
            state_->changed.notify_all();
        }
    };

    class DiagnosticCollection {
        friend class TelemetryGate;
        std::shared_ptr<State> state_;
    public:
        explicit DiagnosticCollection(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}
        DiagnosticCollection(const DiagnosticCollection&)=delete;
        DiagnosticCollection& operator=(const DiagnosticCollection&)=delete;
        ~DiagnosticCollection(){
            { std::lock_guard lock(state_->mutex); --state_->diagnosticCollections; }
            state_->changed.notify_all();
        }
    };

    [[nodiscard]] std::optional<Observation> tryObservation() {
        std::lock_guard lock(state_->mutex);
        if (state_->firmwarePending) return std::nullopt;
        ++state_->observations;
        return Observation(state_,state_->firmwareGeneration,state_->lastFirmwareEndMs,state_->lastFirmwareEventBoundary);
    }

    [[nodiscard]] std::optional<SafetyControl> trySafetyControl() {
        std::lock_guard lock(state_->mutex);
        if (state_->firmwarePending) return std::nullopt;
        ++state_->safetyControls;
        return SafetyControl(state_);
    }

    // A diagnostic recorder is allowed to sample its own sensors. It excludes
    // firmware capture for its entire lifetime, including a still-live child.
    [[nodiscard]] std::shared_ptr<DiagnosticCollection> tryDiagnosticCollection() {
        std::lock_guard lock(state_->mutex);
        if(state_->firmwarePending)return {};
        auto collection=std::make_shared<DiagnosticCollection>(state_);
        ++state_->diagnosticCollections;
        return collection;
    }

    [[nodiscard]] bool owns(const std::shared_ptr<FirmwarePause>& pause)const noexcept {
        return pause&&pause->state_==state_;
    }

    // Close admission without waiting. This is safe on an INFO submission
    // thread: the worker, not the UI, subsequently drains existing readers.
    [[nodiscard]] std::shared_ptr<FirmwarePause> requestFirmwareCapture(std::stop_token token={}) {
        auto state=state_;
        std::lock_guard lock(state->mutex);
        if (token.stop_requested()) throw std::runtime_error("Firmware capture cancelled before telemetry pause");
        if (state->firmwarePending) throw std::runtime_error("Another firmware capture is active");
        if (state->safetyControls) throw std::runtime_error("Firmware capture unavailable: active cooling control requires telemetry");
        if (state->diagnosticCollections) throw std::runtime_error("Firmware capture unavailable: sensor diagnostics are still active");
        state->firmwarePending=true;
        try {
            auto pause=std::make_shared<FirmwarePause>(state);
            ++state->firmwareGeneration;
            return pause;
        } catch (...) {
            state->firmwarePending=false;
            state->changed.notify_all();
            throw;
        }
    }
    // Direct callers use the same admission and wait lifecycle as INFO.
    [[nodiscard]] std::shared_ptr<FirmwarePause> beginFirmwareCapture(
            std::stop_token token, std::chrono::milliseconds timeout) {
        auto pause=requestFirmwareCapture(token);
        pause->waitForQuiescence(token,timeout);
        return pause;
    }
};

inline TelemetryGate& telemetryGate(){
    // Pending external readers may outlive an INFO window or static teardown.
    static auto* gate=new TelemetryGate;
    return *gate;
}

// OEM delivery adapters hold this lease while dispatching the already queued
// event. Timestamps use the same steady_clock milliseconds as the input owner.
inline std::optional<TelemetryGate::Observation> admitOemEvent(std::uint64_t receivedAt){
    auto observation=telemetryGate().tryObservation();
    if(!observation||!observation->acceptsEvent(receivedAt))return {};
    return observation;
}

} // namespace mc
