// Pure offline checks for the common firmware/telemetry ownership contract.
// No OS adapter, real process, hardware driver, GPU, EC, firmware or user files.
#include "MagicControl/TelemetryGate.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

namespace {
using namespace std::chrono_literals;
void check(bool value, const char* detail) {
    if (!value) throw std::runtime_error(detail);
}
template<class Predicate>
bool eventually(Predicate predicate, std::chrono::milliseconds limit = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    do {
        if (predicate()) return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

void idleAndSharedObservations() {
    mc::TelemetryGate gate;
    auto first = gate.tryObservation();
    auto second = gate.tryObservation();
    check(bool(first) && bool(second), "independent idle observations should be admitted");
    first.reset();
    second.reset();
    check(bool(gate.tryObservation()), "dropping observation permits must not block later samples");
}

void generationChangesOnlyOnAdmittedCapture() {
    mc::TelemetryGate gate;
    auto observation=gate.tryObservation();check(bool(observation),"generation fixture observation unavailable");
    const auto before=observation->generation();observation.reset();
    auto ticket=gate.requestFirmwareCapture();
    check(bool(ticket)&&!gate.tryObservation(),"request must close admission synchronously");
    ticket.reset();
    observation=gate.tryObservation();check(bool(observation),"generation fixture did not reopen");
    const auto after=observation->generation();observation.reset();
    check(after!=before,"successful firmware request must advance observation generation");

    auto safety=gate.trySafetyControl();check(bool(safety),"generation fixture safety permit unavailable");
    bool rejected=false;try{(void)gate.requestFirmwareCapture();}catch(const std::runtime_error&){rejected=true;}
    safety.reset();observation=gate.tryObservation();
    check(rejected&&observation&&observation->generation()==after,"rejected firmware request must not advance observation generation");
}

void staleEventBoundaries() {
    mc::TelemetryGate gate;
    auto before=gate.tryObservation();
    check(before&&!before->lastFirmwareEndMs()&&before->acceptsEvent(0),"before any firmware pause timestamps must not be filtered");
    before.reset();
    const auto now=[] {return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    const auto earliest=now();auto ticket=gate.requestFirmwareCapture();ticket.reset();const auto latest=now();
    auto after=gate.tryObservation();check(bool(after)&&after->lastFirmwareEndMs(),"firmware completion timestamp unavailable");
    const auto boundary=*after->lastFirmwareEndMs();
    check(boundary>=earliest&&boundary<=latest,"firmware end timestamp must use the current steady clock");
    check(!after->acceptsEvent(0)&&!after->acceptsEvent(boundary),"zero and equal-time events must be rejected after firmware pause");
    if(boundary>0)check(!after->acceptsEvent(boundary-1),"event older than firmware pause must be rejected");
    check(after->acceptsEvent(boundary+1),"event newer than firmware pause must be accepted");
}

void queuedObservationCannotStart() {
    mc::TelemetryGate gate;
    std::promise<void> submitted, run;
    auto allowedToRun = run.get_future().share();
    std::atomic<unsigned> backendReads{};
    auto queued = std::async(std::launch::async, [&] {
        submitted.set_value();
        allowedToRun.wait();
        if (auto permit = gate.tryObservation()) ++backendReads;
    });
    struct ReleaseQueued {
        std::promise<void>& signal;
        bool sent{};
        void release() { if (!sent) { sent = true; signal.set_value(); } }
        ~ReleaseQueued() { try { release(); } catch (...) {} }
    } releaseQueued{run};
    submitted.get_future().wait();
    std::stop_source stop;
    auto pause = gate.beginFirmwareCapture(stop.get_token(), 1s);
    check(bool(pause), "fixture must begin capture before releasing queued work");
    releaseQueued.release();
    const bool returned = queued.wait_for(3s) == std::future_status::ready;
    if (!returned) { pause.reset(); }
    queued.get();
    check(returned && backendReads == 0, "work queued before capture must recheck admission at execution");
    pause.reset();
    if (auto permit = gate.tryObservation()) ++backendReads;
    check(backendReads == 1, "new observations resume after capture");
}

void blockThenDrain() {
    mc::TelemetryGate gate;
    auto first = gate.tryObservation();
    auto second = gate.tryObservation();
    check(bool(first) && bool(second), "initial observations must start");
    std::stop_source stop;
    auto capture = std::async(std::launch::async, [&] { return gate.beginFirmwareCapture(stop.get_token(), 5s); });

    // Observe the admission transition without reaching into gate internals.
    const bool paused = eventually([&] { return !gate.tryObservation(); });
    const bool deniedSafety = !gate.trySafetyControl();
    const bool waitsWhileBothActive = capture.wait_for(0ms) == std::future_status::timeout;
    first.reset();
    const bool waitsWhileOneActive = capture.wait_for(0ms) == std::future_status::timeout;
    second.reset();
    const bool drained = capture.wait_for(3s) == std::future_status::ready;
    if (!drained) stop.request_stop();
    auto exclusive = capture.get();

    check(paused, "firmware request must close new admissions before waiting");
    check(deniedSafety, "firmware pause must not admit new safety-controlled loops");
    check(waitsWhileBothActive && waitsWhileOneActive, "firmware request must await every in-flight sample");
    check(drained && bool(exclusive), "firmware request must proceed after in-flight observations finish");
    check(!gate.tryObservation(), "background observation must remain closed while capture is active");
    exclusive.reset();
    check(bool(gate.tryObservation()), "normal capture completion must reopen observations");
}

void cancellationAndTimeout() {
    for (const bool cancel : {false, true}) {
        mc::TelemetryGate gate;
        auto inFlight = gate.tryObservation();
        check(bool(inFlight), "fixture sample must enter before cancellation test");
        std::stop_source stop;
        auto capture = std::async(std::launch::async, [&] {
            try { (void)gate.beginFirmwareCapture(stop.get_token(), cancel ? 5s : 500ms); return false; }
            catch (const std::runtime_error&) { return true; }
        });
        const bool paused = eventually([&] { return !gate.tryObservation(); }, 2s);
        if (cancel) stop.request_stop();
        const bool returned = capture.wait_for(2s) == std::future_status::ready;
        if (!returned) { inFlight.reset(); stop.request_stop(); }
        const bool rejected = capture.get();
        const bool reopened = bool(gate.tryObservation());
        inFlight.reset();
        check(paused && returned && rejected, "cancelled or timed-out drain must not admit a capture");
        check(reopened, "cancelled or timed-out drain must reopen new observations");
    }
}

void safetyLoopAdmission() {
    mc::TelemetryGate gate;
    auto control = gate.trySafetyControl();
    check(bool(control), "safety-controlled cooling must start in idle state");
    std::stop_source stop;
    const auto start = std::chrono::steady_clock::now();
    bool rejected = false;
    try { (void)gate.beginFirmwareCapture(stop.get_token(), 200ms); }
    catch (const std::runtime_error&) { rejected = true; }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    check(rejected && elapsed < 2s, "active safety-controlled cooling must refuse firmware capture");
    check(bool(gate.tryObservation()), "safety refusal must not suspend ordinary telemetry");
    control.reset();
    auto exclusive = gate.beginFirmwareCapture(stop.get_token(), 1s);
    check(bool(exclusive), "capture can start after the safety-controlled loop stops");
}

void rejectCompetingCaptureAndPrecancel() {
    mc::TelemetryGate gate;
    std::stop_source activeStop;
    auto first = gate.beginFirmwareCapture(activeStop.get_token(), 1s);
    check(bool(first), "first capture must start");
    bool competingRejected = false;
    try { (void)gate.beginFirmwareCapture(activeStop.get_token(), 100ms); }
    catch (const std::runtime_error&) { competingRejected = true; }
    check(competingRejected && !gate.tryObservation(), "competing capture must not release the first owner's pause");
    first.reset();

    std::stop_source preCancelled;
    preCancelled.request_stop();
    bool cancelRejected = false;
    try { (void)gate.beginFirmwareCapture(preCancelled.get_token(), 1s); }
    catch (const std::runtime_error&) { cancelRejected = true; }
    check(cancelRejected && bool(gate.tryObservation()), "pre-cancelled capture must leave admission open");
}

void diagnosticAndFirmwareAreMutuallyExclusive() {
    mc::TelemetryGate gate;
    auto diagnostic = gate.tryDiagnosticCollection();
    check(bool(diagnostic), "EC diagnostic must start when firmware capture is idle");
    check(bool(gate.tryObservation()), "EC diagnostic must still allow its own observations");
    std::stop_source stop;
    bool rejected = false;
    try { (void)gate.beginFirmwareCapture(stop.get_token(), 100ms); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected && bool(gate.tryObservation()), "active EC diagnostic must reject firmware without stopping its telemetry");

    // An isolated helper can outlive the collector that started it.
    auto child = diagnostic;
    diagnostic.reset();
    rejected = false;
    try { (void)gate.beginFirmwareCapture(stop.get_token(), 100ms); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "pending fake EC helper must keep firmware capture unavailable");
    child.reset();

    auto firmware = gate.beginFirmwareCapture(stop.get_token(), 1s);
    check(bool(firmware) && !gate.tryDiagnosticCollection(), "EC diagnostic cannot start during firmware capture");
    firmware.reset();
    check(bool(gate.tryDiagnosticCollection()), "EC diagnostic admission must return after firmware completion");
}

void pendingChildAndUnwind() {
    // This checks ownership of the generic gate only. Device-specific entry
    // points and native child/reaper adapters require separate integration tests.
    mc::TelemetryGate gate;
    std::stop_source stop;
    auto collector = gate.beginFirmwareCapture(stop.get_token(), 1s);
    check(bool(collector), "fake collector must obtain capture");

    // Simulate the already-started reader outliving collector timeout or UI
    // close. This is a fake ownership model, not a native process test.
    auto child = collector;
    collector.reset();
    check(!gate.tryObservation(), "pending fake child must retain the pause");
    child.reset(); // fake reader has now exited
    check(bool(gate.tryObservation()), "fake reader exit must release the pause");

    bool collectorReached = false;
    try {
        auto failing = gate.beginFirmwareCapture(stop.get_token(), 1s);
        check(bool(failing), "failure fixture must own capture before throwing");
        collectorReached = true;
        throw std::runtime_error("synthetic collector exception");
    } catch (const std::runtime_error&) {}
    check(collectorReached, "failure fixture failed to enter synthetic collector");
    check(bool(gate.tryObservation()), "collector exception must not strand admission");
}
}

int main() {
    try {
        idleAndSharedObservations();
        generationChangesOnlyOnAdmittedCapture();
        staleEventBoundaries();
        queuedObservationCannotStart();
        blockThenDrain();
        cancellationAndTimeout();
        safetyLoopAdmission();
        rejectCompetingCaptureAndPrecancel();
        diagnosticAndFirmwareAreMutuallyExclusive();
        pendingChildAndUnwind();
        std::cout << "PASS telemetry gate: drain, cancellation, timeout, safety, fake child and exception\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL telemetry gate: " << error.what() << '\n';
        return 1;
    }
}
