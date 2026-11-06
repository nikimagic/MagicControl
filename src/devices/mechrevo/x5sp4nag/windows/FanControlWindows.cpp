#include "MagicControl/ResourceControl.h"
#include "MagicControl/ActionLog.h"
#include "FanControlWindows.h"
#include "MagicControl/Logging.h"
#include "PowerDiagnosticsWindows.h"
#include "AmdTelemetryWindows.h"
#include "PawnTelemetryWindows.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/FanCurve.h"
#include "MagicControl/ApplicationPresence.h"
#include "LightingOwnershipWindows.h"
#include "KeysHardwareWindows.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace mc::windows {
// FAN CONTRACT — see FAN_MODES.md and EcRegister.h before editing any gate.
// Explicit Default uses core resetApplicationControl: consumers first, APP last.
// Internal FAN cleanup keeps four-mask-only semantics. Max sets only751.6.
// Autonomous Manual: 7C5.7=0, CPU table only, firmware computes PWM.
// Manual online: 7C5.7=1 ALWAYS, even software CPU=GPU; host computes PWM.
// Never clear tables for a transition or exit. Do not restore from RPM/PWM.
// SETs battery/APU/lighting paths never request a fan reapply.
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::array<std::pair<std::uint16_t, std::uint8_t>, 6> fields{{
    {ec::kRamTableCommit, ec::kRamTableCommitBit}, {ec::kAppPresence, ec::kAppPresenceBit},
    {ec::kControl727, ec::kControl727FanBit}, {ec::kCustomMode, ec::kCustomModeBit},
    {ec::kFanControlRespective, ec::kSplitModeBit},
    {ec::kPerformanceMode, static_cast<std::uint8_t>(ec::kPerformanceBoostBit | ec::kPerformancePresetMask)}}};

std::string stamp() {
    SYSTEMTIME time{}; GetSystemTime(&time);
    return std::format("{:04}-{:02}-{:02}T{:02}-{:02}-{:02}-{:03}Z", time.wYear, time.wMonth,
        time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
}
void winError(const char* action) {
    throw std::runtime_error(std::string(action) + ": Windows " + std::to_string(GetLastError()));
}
struct ExternalChange : std::runtime_error { using std::runtime_error::runtime_error; };
// A failed observation does not prove that another owner changed a gate. Keep
// this distinct from ExternalChange: neither path may blindly re-arm the EC.
struct FanFlagsUnavailable : std::runtime_error { using std::runtime_error::runtime_error; };
struct PowerInterrupted : std::runtime_error {
    PowerInterrupted() : std::runtime_error("Переключение прервано переходом питания.") {}
};
struct DiagnosticRestoreSkipped : std::runtime_error {
    bool superseded;
    DiagnosticRestoreSkipped(std::string reason,bool newer)
        :std::runtime_error(std::move(reason)),superseded(newer){}
};

// Same owner mutexes and competing service as the verified C# controller.
// All acquisition/release occurs on the controller's one worker thread.
struct Lease final : FanOwnership {
    HANDLE cpu{}, gpu{};
    bool ownsCpu{}, ownsGpu{}, restart{};
    SC_HANDLE manager{}, service{};
    explicit Lease(bool pauseService = true) {
        try {
            cpu = CreateMutexW(nullptr, FALSE, L"Local\\Fan_X15Pro_CpuCurveWmi");
            gpu = CreateMutexW(nullptr, FALSE, L"Local\\Fan_X15Pro_GpuCurve");
            if (!cpu || !gpu) winError("Не удалось создать mutex вентиляторов");
            const auto acquire = [](HANDLE handle) {
                const DWORD result = WaitForSingleObject(handle, 0);
                return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
            };
            ownsCpu = acquire(cpu); ownsGpu = ownsCpu && acquire(gpu);
            if (!ownsCpu || !ownsGpu) throw std::runtime_error("Вентиляторами уже управляет другой экземпляр приложения.");
            if (!pauseService) return;
            manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!manager) winError("Нет доступа к диспетчеру служб");
            service = OpenServiceW(manager, L"GCUBridge", SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_START);
            if (!service) {
                if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) return;
                winError("Нет доступа к GCUBridge");
            }
            auto current = state();
            if (current == SERVICE_START_PENDING) { wait(SERVICE_RUNNING); current = SERVICE_RUNNING; }
            restart = current == SERVICE_RUNNING;
            if (restart) {
                SERVICE_STATUS status{};
                if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) winError("Не удалось временно остановить GCUBridge");
                wait(SERVICE_STOPPED);
            } else if (current == SERVICE_STOP_PENDING) wait(SERVICE_STOPPED);
        } catch (...) { close(); throw; }
    }
    DWORD state() const {
        SERVICE_STATUS status{};
        if (!QueryServiceStatus(service, &status)) winError("Не удалось прочитать состояние GCUBridge");
        return status.dwCurrentState;
    }
    void wait(DWORD wanted) const {
        const auto deadline = Clock::now() + 15s;
        while (state() != wanted) {
            if (Clock::now() >= deadline) throw std::runtime_error("GCUBridge не завершила переход за 15 секунд.");
            std::this_thread::sleep_for(100ms);
        }
    }
    void restoreService() {
        if (!service || !restart) return;
        auto current = state();
        if (current == SERVICE_STOP_PENDING) { wait(SERVICE_STOPPED); current = SERVICE_STOPPED; }
        if (current == SERVICE_STOPPED && !StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
            winError("Не удалось вернуть GCUBridge в работу");
        wait(SERVICE_RUNNING); restart = false;
    }
    void close() noexcept {
        try { restoreService(); } catch (...) {}
        if (service) CloseServiceHandle(service);
        if (manager) CloseServiceHandle(manager);
        if (ownsGpu) ReleaseMutex(gpu);
        if (ownsCpu) ReleaseMutex(cpu);
        if (gpu) CloseHandle(gpu);
        if (cpu) CloseHandle(cpu);
        service = manager = nullptr; cpu = gpu = nullptr; ownsCpu = ownsGpu = false;
    }
    ~Lease() override { close(); }
};

struct InstanceLease {
    HANDLE mutex{};
    bool owned{};
    InstanceLease() {
        mutex = CreateMutexW(nullptr, FALSE, L"MagicControl.SingleInstance");
        if (!mutex) winError("Не удалось проверить работающий GUI");
        const auto result = WaitForSingleObject(mutex, 0);
        owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        if (!owned) {
            CloseHandle(mutex); mutex = nullptr;
            throw std::runtime_error("MagicControl уже работает. Выберите профиль в GUI или завершите программу перед командой mode.");
        }
    }
    ~InstanceLease() { if (owned) ReleaseMutex(mutex); if (mutex) CloseHandle(mutex); }
};
struct StandaloneLease final : FanOwnership {
    InstanceLease instance;
    Lease fans{false};
    void restoreService() override { fans.restoreService(); }
};

std::vector<FanCurvePoint> points(const FanCurveTable& table) {
    std::vector<FanCurvePoint> result;
    for (const auto& point : table) if (point.temperature != 255) result.push_back({point.temperature, point.duty});
    std::string error;
    if (!validateFanCurve(result, error)) throw std::runtime_error(error);
    return result;
}
std::array<std::uint8_t, 48> output(int raw, int channel) {
    // Verified EncodeOutput from the reference. CPU retains its firmware
    // emergency stage; software replaces the first six CPU / all GPU duties.
    std::array<std::uint8_t, 48> result{};
    constexpr std::array<std::uint8_t, 16> up{30,40,50,60,70,80,90,100,255,255,255,255,255,255,255,255};
    constexpr std::array<std::uint8_t, 16> down{0,27,37,47,57,67,77,87,97,255,255,255,255,255,255,255};
    std::copy(up.begin(), up.end(), result.begin());
    std::copy(down.begin(), down.end(), result.begin() + 16);
    std::fill(result.begin() + 32, result.end(), static_cast<std::uint8_t>(raw));
    if (channel == 0) { result[5] = 85; result[22] = 82; std::fill(result.begin() + 38, result.end(), std::uint8_t{200}); }
    return result;
}
class DefaultEnvironment final : public FanControlEnvironment {
    EcAccess& ec_;
    std::unique_ptr<AmdTelemetryReader> temperatures_;
    std::unique_ptr<PawnTelemetryReader> cpu_;
public:
    explicit DefaultEnvironment(EcAccess& access) : ec_(access) {}
    MonitoringSnapshot readTemperatures() override {
        if (!temperatures_) temperatures_ = std::make_unique<AmdTelemetryReader>();
        if (!cpu_) cpu_ = std::make_unique<PawnTelemetryReader>(ec_.boardVerified());
        MonitoringSnapshot sample;
        temperatures_->readTemperatures(sample); cpu_->readTemperatures(sample);
        return sample;
    }
    std::unique_ptr<FanOwnership> acquireOwnership(bool pauseService) override { return std::make_unique<Lease>(pauseService); }
    std::unique_ptr<FanOwnership> acquireStandaloneOwnership() override { return std::make_unique<StandaloneLease>(); }
};
}

FanPowerTime FanControlEnvironment::powerTime() const noexcept {
    ULONGLONG awake{};
    const auto elapsed = GetTickCount64();
    return {elapsed, QueryUnbiasedInterruptTime(&awake) ? awake / 10000 : elapsed};
}

namespace {
template<class Apply>PerformanceChange standalonePower(EcAccess& access,FanControlEnvironment* environment,Apply apply){
    access.ensureCapability(DeviceFeature::Fans, "uniwill-x5sp4nag");
    DefaultEnvironment defaultEnvironment(access);
    auto owner = (environment ? environment : &defaultEnvironment)->acquireStandaloneOwnership();
    const EcAccess::Transaction transaction(access);
    if ((access.readStable(ec::kRamTableCommit) & ec::kRamTableCommitBit) ||
        (access.readStable(ec::kPerformanceMode) & ec::kPerformanceBoostBit))
        throw std::runtime_error("В EC активны Manual/Max или оставленная RAM-таблица. Сначала верните Default через владельца вентиляторов.");
    return apply();
}
}
PerformanceChange selectStandalonePerformance(EcAccess& access, int preset, FanControlEnvironment* environment) {
    if (preset < 0 || preset > 2) throw std::invalid_argument("Неизвестный пресет производительности.");
    return standalonePower(access,environment,[&]{return selectEcPerformance(access,preset);});
}
PerformanceChange selectStandaloneCustomPower(EcAccess& access,const std::array<int,3>& values,FanControlEnvironment* environment){
    return standalonePower(access,environment,[&]{return applyCustomPowerLimits(access,values);});
}
PerformanceChange selectStandaloneCustomBase(EcAccess& access,int base,FanControlEnvironment* environment){
    return standalonePower(access,environment,[&]{return selectWujieCustomBase(access,base);});
}

struct FanController::Impl {
    enum class Kind { EcManual, Curve, Native, Maximum, Link, Performance, SavedProfiles, MaximumProbe, DiagnosticPrepare, DiagnosticRestore };
    struct Intent {
        std::array<FanCurveTable,2> curves;
        std::array<std::vector<FanCurvePoint>,2> logical;
        bool linked{},maximum{},ecManual{};
        std::uint8_t mode{};
    };
    struct DiagnosticPlan {
        std::optional<Intent> previous;
        std::string previousMode;
        std::uint64_t generation{},epoch{},commonEpoch{};
        std::array<std::uint8_t,fields.size()> defaultFields{};
        std::atomic<bool> consumed{},writePhaseEntered{};
    };
    struct Request {
        Kind kind; int channel{}; FanCurveTable curve{}; bool linked{}; std::uint64_t revision{};
        std::shared_ptr<DeviceOperation> operation;
        std::shared_ptr<std::promise<PerformanceChange>> performance;
        std::optional<std::array<int,3>> customPower;
        std::optional<int> customBase;
        std::shared_ptr<std::promise<void>> native;
        std::optional<FanCurveProfiles> savedProfiles;
        bool adoptExisting{};
        bool resetApplicationConsumers{};
        std::shared_ptr<std::promise<FanMaximumResult>> probe;
        std::stop_token probeCancellation;
        std::shared_ptr<DiagnosticPlan> diagnosticPlan;
        std::shared_ptr<std::promise<std::shared_ptr<DiagnosticPlan>>> diagnosticPrepared;
        std::shared_ptr<std::promise<FanDiagnosticRestore>> diagnosticRestored;
        bool diagnosticAdmitted{};
        audit::Id auditId{};
    };
    EcAccess& ec;
    std::unique_ptr<FanControlEnvironment> environment;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::optional<Request> pending;
    FanControlStatus state;
    std::uint64_t sequence{};
    std::uint64_t userGeneration{};
    std::atomic<bool> presencePending{true};
    std::atomic<bool> powerSuspended{}, recoveryPending{};
    std::optional<unsigned> displayState;
    std::atomic<std::shared_ptr<const std::function<void()>>> configurationRestoreNotifier;
    std::atomic<std::uint64_t> powerEpoch{};
    std::atomic<ULONGLONG> lastResumeAt{};
    std::shared_ptr<DeviceOperation> powerOperation;
    std::unique_ptr<EcAccess::ObservationPause> powerPause;
    std::atomic<ULONGLONG> lastKeyboardAt{}, keyboardDue{};
    mc::ResourceActor worker;

    explicit Impl(EcAccess& access, std::unique_ptr<FanControlEnvironment> source)
        : ec(access), environment(source ? std::move(source) : std::make_unique<DefaultEnvironment>(access)),
          worker([this](std::stop_token token) { run(token); }) {}
    ~Impl() { worker.request_stop(); condition.notify_all(); if (worker.joinable()) worker.join(); }
    void log(const std::string& value) noexcept {
        if (value.starts_with("FEEDBACK ") && !powerDiagnosticBurstActive()) return;
        logMessage("fan_control", value);
    }
    void request(Request value) {
        audit::Scope trace("fans.queue","\"kind\":"+std::to_string(static_cast<int>(value.kind))+",\"fan_channel\":"+std::to_string(value.channel)+",\"linked\":"+(value.linked?"true":"false"),value.operation?value.operation->actionId():audit::capture());
        value.auditId=audit::current();
        ec.ensureCapability(DeviceFeature::Fans, "uniwill-x5sp4nag");
        if (powerSuspended.load() || recoveryPending.load())
            throw std::runtime_error("Ожидайте восстановления EC после перехода питания.");
        if (value.kind == Kind::Curve) static_cast<void>(points(value.curve));
        if (value.savedProfiles) {
            const auto& saved = *value.savedProfiles;
            const bool cpuMax = saved.currentPreset(0) == FanCurvePreset::Max;
            const bool gpuMax = saved.currentPreset(1) == FanCurvePreset::Max;
            if (cpuMax != gpuMax)
                throw std::invalid_argument("Сохранённый Max должен быть выбран для обоих вентиляторов.");
            if (!cpuMax) for (int channel = 0; channel < 2; ++channel)
                if (saved.currentPreset(channel) != FanCurvePreset::Default)
                    static_cast<void>(points(saved.current(channel)));
        }
        if (value.kind == Kind::Performance && (value.channel < 0 || value.channel > 2))
            throw std::invalid_argument("Неизвестный пресет производительности.");
        if(value.customPower)for(const auto watts:*value.customPower)
            if(watts<5||watts>kApuManualMaximum)throw std::invalid_argument("Custom: допустимо 5–255 Вт");
        if (value.operation && !deviceOperations().owns(value.operation)) throw std::runtime_error("Недействительная блокировка EC.");
        if (!value.operation) value.operation = deviceOperations().tryBegin();
        if (!value.operation) throw std::runtime_error("Ожидайте завершения текущего переключения EC.");
        { std::lock_guard lock(mutex);
            if (state.applying || pending) throw std::runtime_error("Ожидайте завершения применения кривой или пресета.");
            if (value.kind == Kind::Link && (!state.owned || state.ecManual || state.maximum ||
                (!state.controlled[0] && !state.controlled[1])))
                throw std::runtime_error("CPU=GPU доступна только в Manual online.");
            if (state.needsRestore && value.kind != Kind::Native && value.kind != Kind::DiagnosticPrepare)
                throw std::runtime_error("Сначала завершите возврат штатного режима через Default.");
            if(value.kind==Kind::DiagnosticRestore&&value.diagnosticPlan&&
                (value.diagnosticPlan->generation!=userGeneration||value.diagnosticPlan->epoch!=powerEpoch.load()||
                    value.diagnosticPlan->commonEpoch!=diagnosticFanEpoch()))
                throw DiagnosticRestoreSkipped("Diagnostic FAN restore superseded by a newer command/power epoch",true);
            if(value.kind!=Kind::MaximumProbe&&value.kind!=Kind::Performance&&
                value.kind!=Kind::DiagnosticPrepare&&value.kind!=Kind::DiagnosticRestore)++userGeneration;
            value.revision = ++sequence; pending = std::move(value);
            state.observedDefault.reset();
            state.busy = true; state.applying = true; state.error = false; state.message = pending->adoptExisting ? "Проверяю настройки EC…" : "Применяю настройки EC…";
        }
        condition.notify_all();trace.finish("queued");
    }
    FanControlStatus status() const { std::lock_guard lock(mutex); return state; }
    FanDiagnosticRestore restoreDiagnostic(const std::shared_ptr<DiagnosticPlan>& plan) {
        if(std::this_thread::get_id()==worker.get_id())return {false,false,"Diagnostic FAN restore cannot await its own worker",false};
        if(plan->consumed.exchange(true))return {false,false,"Diagnostic FAN restore plan was already consumed",false};
        const auto deadline=Clock::now()+5s;
        for(;;){
            {std::lock_guard lock(mutex);
                if(plan->generation!=userGeneration||plan->epoch!=powerEpoch.load()||plan->commonEpoch!=diagnosticFanEpoch())
                    return {false,false,"Diagnostic FAN restore superseded by a newer command/power epoch",true};}
            auto operation=deviceOperations().tryBegin();
            if(!operation){if(Clock::now()<deadline){std::this_thread::sleep_for(25ms);continue;}
                return {false,false,"Diagnostic FAN restore: device operation remained busy",false};}
            auto result=std::make_shared<std::promise<FanDiagnosticRestore>>();auto future=result->get_future();
            Request requestValue{Kind::DiagnosticRestore};requestValue.operation=std::move(operation);
            requestValue.diagnosticPlan=plan;requestValue.diagnosticRestored=result;
            try{request(std::move(requestValue));return future.get();}
            catch(const DiagnosticRestoreSkipped& e){return {false,false,e.what(),e.superseded};}
            catch(const std::exception& e){
                std::lock_guard lock(mutex);const bool newer=plan->generation!=userGeneration||plan->epoch!=powerEpoch.load()||plan->commonEpoch!=diagnosticFanEpoch();
                return {plan->writePhaseEntered.load(),false,e.what(),newer};}
        }
    }
    void notifyConfigurationRestore() {
        const auto notify = configurationRestoreNotifier.load();
        if (notify) (*notify)();
    }
    void suspendPower() {
        { std::lock_guard lock(mutex);
            if (powerSuspended.exchange(true)) return;
            ++powerEpoch;
            state.suspended = state.busy = state.applying = true;
            // После сна прошивка может изменить режим. Старое наблюдение нельзя
            // выдавать за подтверждение до нового чтения EC после пробуждения.
            state.observedDefault.reset();
            state.message = "Управление EC приостановлено на время сна.";
            if (pending) {
                const auto failure = std::make_exception_ptr(PowerInterrupted());
                if (pending->performance) pending->performance->set_exception(failure);
                if (pending->native) pending->native->set_exception(failure);
                if (pending->diagnosticPrepared) pending->diagnosticPrepared->set_exception(failure);
                if (pending->diagnosticRestored) pending->diagnosticRestored->set_exception(failure);
                if (pending->probe) pending->probe->set_exception(failure);
                pending.reset();
            }
            if (!powerOperation) powerOperation = deviceOperations().tryBegin();
            if (!powerPause) powerPause = std::make_unique<EcAccess::ObservationPause>(ec);
        }
        condition.notify_all();
    }
    void resumePower(bool sourceChanged) {
        { std::lock_guard lock(mutex);
            const auto now = GetTickCount64();
            const bool wasSuspended = powerSuspended.exchange(false);
            const auto previous = lastResumeAt.load();
            if (!wasSuspended && !sourceChanged && (recoveryPending.load() || powerPause)) return;
            if (!wasSuspended && !sourceChanged && previous && now - previous < 1000) return;
            lastResumeAt.store(now); recoveryPending.store(true); ++powerEpoch;
            state.busy = state.applying = true; state.suspended = false;
            state.observedDefault.reset();
            state.message = "Ожидайте восстановления EC после перехода питания.";
            if (!powerOperation) powerOperation = deviceOperations().tryBegin();
            if (!powerPause) powerPause = std::make_unique<EcAccess::ObservationPause>(ec);
        }
        condition.notify_all();
    }
    void sessionDisplayChanged(unsigned value) {
        if (value > 2) return;
        bool resumedFromSleep{};
        {
            std::lock_guard lock(mutex);
            resumedFromSleep = displayState && *displayState == 0 && value != 0 && powerSuspended.load();
            displayState = value;
        }
        // Display-on alone is not a wake event. This is a fallback only for a
        // confirmed suspend whose resume notification Windows did not deliver.
        if (resumedFromSleep) { resumePower(false); notifyConfigurationRestore(); }
    }

    void run(std::stop_token token) {
        std::unique_ptr<FanOwnership> lease;
        std::optional<TelemetryGate::SafetyControl> ownershipSafety;
        // A failed service restart must not remain an active FAN lease, but
        // its original restart intent/handles must survive an explicit retry.
        std::unique_ptr<FanOwnership> pendingServiceRestore;
        bool flagsOnlyLease{};
        std::shared_ptr<DeviceOperation> activeOperation;
        std::map<std::uint16_t, std::uint8_t> expected;
        std::array<FanCurveTable, 2> curves{};
        std::array<std::vector<FanCurvePoint>, 2> logical;
        std::array<int, 2> level{}, lastRaw{-1,-1};
        std::array<bool,2> tableCacheValid{};
        bool linked{}, maximum{}, ecManual{}, touched{}, reload{}, startupConfirmationPending{};
        bool ownershipUnknown{},automaticRestoreWrites{},deferredPresence{};
        std::uint64_t revision{};
        auto nextIdleObservation=Clock::time_point{};
        std::optional<Intent> confirmed;
        bool recovering{};
        auto nextEcFlagCheck = Clock::now();
        auto recoveryStarted = Clock::now();
        auto cycleEpoch = powerEpoch.load();
        FanSleepTracker sleepTracker;
        static_cast<void>(sleepTracker.observe(environment->powerTime()));
        const auto interruptedByPower = [&] {
            if (sleepTracker.observe(environment->powerTime())) {
                ++powerEpoch;
                log("SYSTEM SLEEP: elapsed/unbiased clocks confirm suspension; recover current session");
                notifyConfigurationRestore();
            }
            return powerSuspended.load() || powerEpoch.load() != cycleEpoch;
        };
        const auto checkPower = [&] {
            if (interruptedByPower()) throw PowerInterrupted();
        };
        const auto finishPowerOperation = [&] {
            std::lock_guard lock(mutex);
            if (powerSuspended.load() || powerEpoch.load() != cycleEpoch) return;
            if (powerOperation || powerPause) lastResumeAt.store(GetTickCount64());
            powerOperation.reset(); powerPause.reset(); state.suspended = false;
        };
        const auto commandFailed = [](const std::optional<Request>& command, std::exception_ptr error) {
            if (command && command->performance) command->performance->set_exception(error);
            if (command && command->native) command->native->set_exception(error);
            if (command && command->diagnosticPrepared) command->diagnosticPrepared->set_exception(error);
            if (command && command->diagnosticRestored) command->diagnosticRestored->set_exception(error);
            if (command && command->probe) command->probe->set_exception(error);
        };
        const auto interrupted = [&](const std::optional<Request>& command) {
            recovering = true; recoveryStarted = Clock::now();
            tableCacheValid.fill(false); lastRaw = {-1,-1};
            commandFailed(command, std::make_exception_ptr(PowerInterrupted()));
            { std::lock_guard lock(mutex);
                if (!powerOperation) powerOperation = activeOperation ? activeOperation : deviceOperations().tryBegin();
                state.suspended = powerSuspended.load(); state.busy = state.applying = true;
                state.message = "Переход питания: подтверждённые кривые сохранены для восстановления.";
            }
            activeOperation.reset();
        };
        const auto readTemperatures = [&] {
            checkPower();
            MonitoringSnapshot sample;
            try { sample = environment->readTemperatures(); }
            catch (...) { checkPower(); throw; }
            checkPower();
            if (!sample.processorSampledAtMs || GetTickCount64() - sample.processorSampledAtMs > 10000)
                throw std::runtime_error("Нет свежих температур CPU/GPU; управление возвращено EC.");
            return sample;
        };
        const auto write = [&](std::uint16_t address, std::uint8_t value, std::uint8_t mask = 255) {
            checkPower();
            const auto actual = ec.readStable(address);
            if(automaticRestoreWrites&&expected.contains(address)&&((actual^expected.at(address))&mask))
                throw ExternalChange("Diagnostic FAN restore: owned field changed before masked write");
            const auto wanted = static_cast<std::uint8_t>((actual & ~mask) | (value & mask));
            if (wanted != actual) {
                touched = true;
                if (mask == 255) ec.writeVerified(address, wanted);
                else {
                    expected[address] = ec.updateBitsVerified(address, mask, value);
                    return;
                }
            }
            expected[address] = wanted;
        };
        const auto flagsMatch = [&] {
            checkPower();
            // Only six owned control fields, never either RAM table. APU mode,
            // breathing, camera and firmware status bits belong to other paths.
            for (const auto [address, fieldMask] : fields) {
                const auto mask = address == ec::kPerformanceMode ? ec::kPerformanceBoostBit : fieldMask;
                bool mismatch = true;
                try {
                    for (int attempt = 0; attempt < 3; ++attempt) {
                        checkPower();
                        if (((ec.readStable(address) ^ expected.at(address)) & mask) == 0) {
                            mismatch = false;
                            break;
                        }
                        if (attempt < 2) std::this_thread::sleep_for(20ms);
                    }
                } catch (const PowerInterrupted&) { throw; }
                  catch (const std::exception& error) {
                    throw FanFlagsUnavailable(std::string("Не удалось проверить флаги FANs: ") + error.what());
                }
                if (mismatch) return false;
            }
            checkPower();
            return true;
        };
        const auto check = [&](bool) {
            if (!flagsMatch()) throw ExternalChange("Флаги управления вентиляторами EC изменились.");
        };
        const auto begin = [&](bool flagsOnly = false,bool allowServicePause = true,bool requiresCoolingSafety = false) {
            if (lease && !flagsOnly && flagsOnlyLease) {
                lease->restoreService(); lease.reset();
            }
            const bool newSafety=requiresCoolingSafety&&!ownershipSafety;
            if(newSafety){ownershipSafety=telemetryGate().trySafetyControl();
                if(!ownershipSafety)throw std::runtime_error("Firmware capture is active; fan control not started");}
            if (lease) return;
            try{
                checkPower();
                ec.ensureCapability(DeviceFeature::Fans, "uniwill-x5sp4nag");
                logical = {}; curves = {}; lastRaw = {-1,-1}; level = {}; maximum = false; ecManual = false; reload = false;
                lease = environment->acquireOwnership(!flagsOnly&&allowServicePause);
            }
            catch(...){if(newSafety)ownershipSafety.reset();throw;}
            flagsOnlyLease = flagsOnly||!allowServicePause;
            checkPower();
            EcAccess::ObservationPause pause(ec); const EcAccess::Transaction transaction(ec);
            expected.clear(); touched = false; tableCacheValid.fill(false);
            // Table contents are read once by the channel writer, never again for backup.
            for (const auto& [address, mask] : fields) { (void)mask; expected[address] = ec.readStable(address); }
            maximum = (expected.at(ec::kPerformanceMode) & ec::kPerformanceBoostBit) != 0;
            const auto directory = portableSettingsPath("fan-control.log").parent_path() / "Backups" /
                ("fan-" + stamp() + "-" + std::to_string(GetCurrentProcessId()));
            std::filesystem::create_directories(directory);
            std::ofstream backup(directory / "ec-before.csv", std::ios::binary);
            if (!backup) throw std::runtime_error("Не удалось сохранить исходное состояние EC.");
            backup << "Address,Value\r\n";
            for (const auto& [address,value] : expected) backup << std::format("0x{:04X},{}\r\n",address,value);
            backup.flush();
            if (!backup) throw std::runtime_error("Не удалось записать резервную копию EC.");
            const auto pathText = directory.u8string();
            log("OWNER acquired; baseline=" + std::string(reinterpret_cast<const char*>(pathText.data()), pathText.size()));
        };
        const auto disableCopilot = [&] {
            applyKeyHardware({"wu-jie-15-xpro",false,&ec,nullptr,nullptr},KeyAction::Copilot,false);
        };
        const auto native = [&](bool resetConsumers=false) {
            checkPower();
            EcAccess::ObservationPause pause(ec); const EcAccess::Transaction transaction(ec);
            touched = true;
            if(resetConsumers)resetApplicationControl(ec,disableCopilot,checkPower);
            else restoreNativeFanMode(ec);
            for (const auto [address,mask] : {
                std::pair{ec::kRamTableCommit,ec::kRamTableCommitBit},
                std::pair{ec::kCustomMode,ec::kCustomModeBit},
                std::pair{ec::kControl727,ec::kControl727FanBit},
                std::pair{ec::kPerformanceMode,ec::kPerformanceBoostBit}})
                if (ec.readStable(address) & mask) throw std::runtime_error("EC не подтвердил штатный режим.");
            for (auto& [address,value] : expected)
                if (address < ec::kCpuTableBase) value = ec.readStable(address);
            logical = {}; lastRaw = {-1,-1}; maximum = false; ecManual = false;
        };
        const auto release = [&] {
            // Detach logical ownership even if returning GCUBridge fails.
            // Otherwise the next ordinary cycle could retain a stale lease
            // after fail() had already reported that control was released.
            auto releasing=lease?std::move(lease):std::move(pendingServiceRestore);
            expected.clear(); touched = false; confirmed.reset(); tableCacheValid.fill(false);
            ownershipUnknown = false;
            if(releasing)try{releasing->restoreService();}
            catch(...){
                // Ownership has been detached, but a failed service restore
                // still requires an explicit successful Default retry before
                // any subsequent interactive FAN or power operation.
                pendingServiceRestore=std::move(releasing);
                {std::lock_guard lock(mutex);state.needsRestore=true;}
                throw;
            }
            ownershipSafety.reset();
        };
        const auto diagnosticDefaultCheck = [&](const DiagnosticPlan& plan,bool requireOwner = true) {
            checkPower();
            {std::lock_guard lock(mutex);
                if(plan.generation!=userGeneration||plan.epoch!=powerEpoch.load()||plan.commonEpoch!=diagnosticFanEpoch())
                    throw DiagnosticRestoreSkipped("Diagnostic FAN restore superseded by a newer command/power epoch",true);}
            // Automatic replay may not stop an OEM service. Reuse the existing
            // read-only SCM/process admission; its status is diagnostic text,
            // not a claim about HID presence or an exclusive external lease.
            if(requireOwner){const auto owner=inspectLightingOwnership();
                if(!owner.writable)throw DiagnosticRestoreSkipped("Diagnostic FAN restore declined: "+owner.status,false);}
            try{
                const EcAccess::Transaction transaction(ec);
                if(!readNativeFanFlags(ec).defaultMode)
                    throw DiagnosticRestoreSkipped("Diagnostic FAN restore declined: fresh Default is not confirmed",false);
                for(std::size_t i=0;i<fields.size();++i){
                    const auto [address,fieldMask]=fields[i];
                    const auto mask=address==ec::kPerformanceMode?ec::kPerformanceBoostBit:fieldMask;
                    if((ec.readStable(address)&mask)!=(plan.defaultFields[i]&mask))
                        throw DiagnosticRestoreSkipped("Diagnostic FAN restore declined: owned Default baseline changed externally",false);
                }
                checkPower();
            }catch(const DiagnosticRestoreSkipped&){throw;}
             catch(const PowerInterrupted&){throw;}
             catch(const std::exception& e){throw DiagnosticRestoreSkipped(std::string("Diagnostic FAN restore declined: Default observation unavailable: ")+e.what(),false);}
        };
        const auto preparedDiagnostic = [&](Request& requestValue) {
            if(!requestValue.diagnosticPrepared)return;
            auto& plan=*requestValue.diagnosticPlan;
            {const EcAccess::Transaction transaction(ec);checkPower();
                if(!readNativeFanFlags(ec).defaultMode)throw std::runtime_error("Diagnostic FAN preparation: Default readback not confirmed");
                for(std::size_t i=0;i<fields.size();++i)plan.defaultFields[i]=ec.readStable(fields[i].first);
                checkPower();}
            requestValue.diagnosticPrepared->set_value(requestValue.diagnosticPlan);
        };
        const auto restoredDiagnostic = [&](const std::optional<Request>& command) {
            if(!command||!command->diagnosticRestored)return;
            checkPower();
            command->diagnosticRestored->set_value({true,true,"Pre-dump FANs intent restored; table/flags readback confirmed, physical execution not measured",false});
        };
        const auto restorePresence = [&] {
            try {
                checkPower();
                EcAccess::ObservationPause pause(ec); const EcAccess::Transaction transaction(ec);
                // A queued APP notification is not permission to reclaim a fan
                // field before the watchdog has observed a foreign/unknown
                // owner. With a retained lease it must pass the same masks.
                if(lease)check(false);
                const auto consumers=readApplicationPresence(ec);
                // No unconditional startup/resume APP acquisition. FAN recovery
                // belongs to its existing owner; only a latched Copilot request
                // can require presence here. Explicit Default clears that latch.
                if(!consumers.copilot){log("APP PRESENCE skipped: no Copilot consumer; fan owner controls its own presence");return;}
                const auto before = ec.readStable(ec::kAppPresence);
                if (!(before & ec::kAppPresenceBit) &&
                    (ec.readStable(ec::kRamTableCommit) & ec::kRamTableCommitBit)) {
                    log("APP PRESENCE deferred: retained RAM table must be activated by its fan owner");
                    return;
                }
                auto after = before;
                if (!(before & ec::kAppPresenceBit))
                    after = ec.updateBitsVerified(ec::kAppPresence, ec::kAppPresenceBit, ec::kAppPresenceBit);
                if (lease) expected[ec::kAppPresence] = after;
                log(std::format("APP PRESENCE confirmed: 0x741=0x{:02X}->0x{:02X}; native cooling preserved",before,after));
            } catch (const std::exception& error) { log(std::string("APP PRESENCE ERROR: ")+error.what()); }
        };
        const auto wakeKeyboard = [&] {
            if (deviceOperations().busy() || ec.observationsPaused()) return;
            try {
                checkPower();
                EcAccess::ObservationPause pause(ec); const EcAccess::Transaction transaction(ec);
                if (!(ec.readStable(ec::kAppPresence) & ec::kAppPresenceBit) ||
                    !(ec.readStable(ec::kKeyboardCapability) & ec::kKeyboardRgbBit)) return;
                const auto before = ec.readStable(ec::kKeyboardLevel);
                const auto brightness = (before >> 5) & 7;
                if (brightness < 1 || brightness > 4 || (before & 2)) return;
                // Read-modify-write with no setting mask preserves a simultaneous
                // Fn brightness/Off change; only the apply strobe is requested.
                const auto after = ec.updateBitsVerified(ec::kKeyboardLevel,0,0,ec::kKeyboardStrobe);
                log(std::format("KEYBOARD WAKE: 0x78C=0x{:02X}->0x{:02X}; current level preserved",before,after));
            } catch (const std::exception& error) { log(std::string("KEYBOARD WAKE ERROR: ")+error.what()); }
        };
        const auto verifyRecoveryOwner = [&] {
            try {
            checkPower();
            const EcAccess::Transaction transaction(ec);
            const int preset = ec.readStable(ec::kPerformanceMode) & ec::kPerformancePresetMask;
            checkPower();
            if (preset != 0 && preset != 0x10 && preset != 0x80)
                throw ExternalChange("После пробуждения выбран неизвестный режим EC; восстановление отменено.");
            const bool activeRam = (ec.readStable(ec::kRamTableCommit) & ec::kRamTableCommitBit) &&
                (ec.readStable(ec::kAppPresence) & ec::kAppPresenceBit);
            checkPower();
            if (activeRam && !maximum) for (std::uint16_t address = ec::kCpuTableBase; address < (ecManual ? ec::kGpuTableBase : ec::kTableEnd); ++address) {
                // Autonomous Manual never writes Down[0]; firmware is allowed
                // to change that reserved byte across sleep without becoming a
                // foreign owner of our actual curve payload.
                if(ecManual&&address==ec::kCpuTableBase+16)continue;
                checkPower();
                const auto actual = ec.readStable(address); checkPower();
                if (actual != expected.at(address))
                    throw ExternalChange("RAM-таблица изменена другим владельцем; восстановление отменено.");
            }
            }catch(const PowerInterrupted&){throw;}
             catch(const ExternalChange&){throw;}
             catch(const std::exception& error){
                // A wake-time RAM/flag read failure is unknown ownership, not
                // a generic error that may fall back to Default cleanup.
                throw FanFlagsUnavailable(std::string("После сна владелец FANs не подтверждён: ")+error.what());
            }
        };
        while (!token.stop_requested()) {
            std::optional<Request> command;
            { std::unique_lock lock(mutex);
                if (powerSuspended.load()) {
                    condition.wait(lock, [&] { return token.stop_requested() || !powerSuspended.load(); });
                    if (token.stop_requested()) break;
                }
                const auto due = keyboardDue.load();
                auto delay = 1000ms;
                if (due && !recovering) {
                    const auto now = GetTickCount64();
                    delay = std::min(delay,std::chrono::milliseconds(due > now ? due-now : 0));
                }
                condition.wait_for(lock, delay, [&] { return pending.has_value() || (!recovering && presencePending.load()) || recoveryPending.load() || powerSuspended.load() ||
                    keyboardDue.load()!=due || token.stop_requested(); });
                if (token.stop_requested()) break;
                if (powerSuspended.load()) continue;
                command = std::exchange(pending, std::nullopt);
            }
            audit::Scope commandTrace("fans.execute",command?("\"kind\":"+std::to_string(static_cast<int>(command->kind))+",\"revision\":"+std::to_string(command->revision)):"",command?command->auditId:0,command.has_value());
            struct FinishAudit{Impl* self;audit::Scope* trace;bool enabled;~FinishAudit(){if(!enabled)return;try{const auto s=self->status();trace->finish(s.error?"failed":s.applying?"pending":"worker_returned","\"message\":"+audit::quote(s.message)+",\"revision\":"+std::to_string(s.revision)+",\"confirmed_channels\":["+(s.confirmed[0]?"true":"false")+","+(s.confirmed[1]?"true":"false")+"],\"needs_restore\":"+(s.needsRestore?"true":"false"));}catch(...){}}} finishAudit{this,&commandTrace,command.has_value()};
            const bool slept = sleepTracker.observe(environment->powerTime());
            if (slept) {
                ++powerEpoch;
                log("SYSTEM SLEEP: elapsed/unbiased clocks confirm suspension; recover current session");
                notifyConfigurationRestore();
            }
            if (recoveryPending.exchange(false) || slept) {
                tableCacheValid.fill(false); lastRaw = {-1,-1};
                recovering = true; recoveryStarted = Clock::now();
            }
            cycleEpoch = powerEpoch.load();
            if (!recovering && (presencePending.exchange(false)||deferredPresence)) {
                if(auto observation=telemetryGate().tryObservation()){deferredPresence=false;restorePresence();}
                else deferredPresence=true;
            }
            auto due = keyboardDue.load();
            if (!recovering && due && GetTickCount64() >= due && keyboardDue.compare_exchange_strong(due,0)) {
                if(auto observation=telemetryGate().tryObservation())wakeKeyboard();
                else keyboardDue.store(GetTickCount64()+1000);
            }
            if (!command && !lease && !recovering) {
                // На старте Default может уже совпадать с сохранённым: restorer
                // правильно пропускает применение, а command revision остаётся0.
                // Независимое чтение публикует фактический режим в общем status
                // для SETs и трея. Здесь нет захвата OEM-владения, записей флагов,
                // лимитов APU или таблиц. На время сна/чужой операции чтение ждёт.
                if(Clock::now()>=nextIdleObservation&&!deviceOperations().busy()&&!ec.observationsPaused()){
                    nextIdleObservation=Clock::now()+1s;
                    auto observation=telemetryGate().tryObservation();
                    if(!observation)continue;
                    try{
                        checkPower();const auto actual=readNativeFanFlags(ec);checkPower();
                        std::lock_guard lock(mutex);
                        if(!pending&&!state.busy&&!state.suspended){
                            if(state.observedDefault!=actual.defaultMode)
                                log(std::format("FAN OBSERVED: Default={} active={} boost={}; read-only flags, revision={}",actual.defaultMode,actual.active,actual.boost,state.revision));
                            state.observedDefault=actual.defaultMode;
                        }
                    }catch(const std::exception& error){
                        nextIdleObservation=Clock::now()+5s;
                        // Ошибка чтения означает «неизвестно», а не Default.
                        // Не лечить её аппаратным переключением/восстановлением.
                        std::lock_guard lock(mutex);state.observedDefault.reset();
                        log(std::string("FAN OBSERVED unavailable: ")+error.what());
                    }
                }
                continue;
            }
            // A lease used only for autonomous EC modes is not a temperature
            // control loop. Its optional periodic check is paused with other
            // observations; a real online loop instead holds ownershipSafety.
            std::optional<TelemetryGate::Observation> passiveObservation;
            if(!command&&!ownershipSafety&&!recovering){
                passiveObservation=telemetryGate().tryObservation();
                if(!passiveObservation)continue;
            }
            bool external{};
            automaticRestoreWrites=false;
            std::optional<PerformanceChange> performanceResult;
            bool onlineReconfigure{};
            // A queued request already owns DeviceOperation (possibly shared
            // with ConfigurationRestore). Recovery must use that same lease,
            // not tryBegin() against itself and drop the request on continue.
            if (command) { revision = command->revision; activeOperation = command->operation; }
            const auto deferRecoveryCommand = [&] {
                std::unique_lock lock(mutex);
                if (command) {
                    if (powerSuspended.load() || powerEpoch.load() != cycleEpoch) throw PowerInterrupted();
                    pending = std::move(command); command.reset(); activeOperation.reset();
                }
                condition.wait_for(lock, 100ms, [&] { return token.stop_requested() || powerSuspended.load(); });
            };
            try {
                checkPower();
                if (recovering) {
                    // Every confirmed wake must revalidate identity before the
                    // unchanged-flags fast path can mark recovery complete.
                    // Keep the original gate/pause and ten-second grace period.
                    { std::lock_guard lock(mutex);
                        if (!activeOperation && !powerOperation) powerOperation = deviceOperations().tryBegin();
                        if (!activeOperation && !powerOperation) continue;
                        state.busy = state.applying = true;
                        if (!powerPause) powerPause = std::make_unique<EcAccess::ObservationPause>(ec);
                        state.message = "Восстанавливаю выбранный режим вентиляторов по флагам EC…";
                    }
                    try {
                        bool identityReady = true;
                        for (const auto [address, value] : {
                            std::pair{ec::kBoardEcVersion, ec::kBoardEcVersionExpected},
                            std::pair{ec::kBoardSystemId, ec::kBoardSystemIdExpected},
                            std::pair{ec::kBoardProjectId, ec::kBoardProjectIdExpected}}) {
                            const auto actual = ec.readStable(address); checkPower();
                            identityReady &= actual == value;
                        }
                        if (!identityReady) {
                            if (Clock::now() - recoveryStarted < 10s) { deferRecoveryCommand(); continue; }
                            ec.invalidateBoardVerification();
                            throw ExternalChange("Плата после пробуждения не подтверждена; записи EC отключены до перезапуска.");
                        }
                    } catch (const ExternalChange&) { throw; }
                      catch (const PowerInterrupted&) { throw; }
                      catch (const std::exception&) {
                        checkPower();
                        if (Clock::now() - recoveryStarted < 10s) { deferRecoveryCommand(); continue; }
                        ec.invalidateBoardVerification();
                        throw ExternalChange("Не удалось проверить плату после сна; записи EC отключены до перезапуска.");
                    }
                }
                const bool ecObservationDue = ecManual && Clock::now() >= nextEcFlagCheck;
                if (lease && confirmed && !command && (recovering || ecObservationDue || maximum || ownershipUnknown)) {
                    nextEcFlagCheck = Clock::now() + 5s;
                    bool matches;
                    try { const EcAccess::Transaction transaction(ec); matches = flagsMatch(); }
                    catch (const PowerInterrupted&) { throw; }
                    catch (const std::exception& error) {
                        // Unknown is not a mismatch or permission to restore. Stop
                        // this cycle's writes and withhold actual confirmation.
                        ownershipUnknown = true;
                        log(std::string("FAN FLAGS unavailable; writes paused: ") + error.what());
                        { std::lock_guard lock(mutex);
                          state.confirmed = {}; state.observedDefault.reset();
                          // A request may arrive during the old read-only poll.
                          // Its progress belongs to the new command generation.
                          if(sequence==revision){state.applying = state.busy = false;
                              state.message = std::string("Режим FANs не подтверждён; запись приостановлена: ") + error.what();} }
                        finishPowerOperation();
                        continue;
                    }
                    ownershipUnknown = false;
                    if (matches && recovering && confirmed->ecManual) {
                        // Resume can preserve flags while the firmware table executor is dormant.
                        // Re-arm once for confirmed sleep; periodic flag checks still do not rewrite tables.
                        log("EC_MANUAL wake: flags retained; reloading tables and re-arming firmware executor");
                    } else if (matches && recovering) {
                        // Identical flags do not prove that the retained RAM
                        // table is still ours. Check it before the normal loop
                        // can write the next target over another owner's data.
                        verifyRecoveryOwner();
                        recovering = false;
                        { std::lock_guard lock(mutex); state.applying = state.busy = false; }
                        finishPowerOperation();
                        log("FAN FLAGS unchanged after wake; fan restore skipped");
                    } else if (!matches) {
                        // A periodic poll cannot turn a foreign selection into
                        // wake recovery. Only confirmed sleep already set recovering.
                        if (!recovering) throw ExternalChange("Флаги FANs изменены извне; управление остановлено без записей EC.");
                        tableCacheValid.fill(false); lastRaw = {-1,-1};
                        log("FAN FLAGS mismatch after confirmed sleep; checking retained owner before recovery");
                    }
                }
                if (ecObservationDue && ecManual && !recovering) {
                    try {
                        const int cpuEc = ec.readTemperature(ec::kCpuTemperature);
                        const auto candidate = ec.readStable(0x44F);
                        const auto last = status();
                        log(std::format("EC_MANUAL OBSERVE CPU_EC_43E={}C candidate_44F_raw={} (GPU identity unverified); previous_cycle CPU_PWM={} CPU_RPM={} GPU_PWM={} GPU_RPM={}; EC_writes={}; no host PWM calculation",
                            cpuEc,candidate,last.actual[0].value_or(-1),last.rpm[0].value_or(-1),
                            last.actual[1].value_or(-1),last.rpm[1].value_or(-1),ec.writeCount()));
                    } catch (const std::exception& error) { log(std::string("EC_MANUAL observation unavailable: ") + error.what()); }
                }
                // A new user command supersedes recovery of the old intention.
                if (command) recovering = false;
                if (recovering) {
                    // Identity and the gate were checked above, including for
                    // retained flags. Never adopt another session's RAM.
                    if (lease && confirmed) {
                        verifyRecoveryOwner();
                        MonitoringSnapshot fresh;
                        bool available = true;
                        if (!confirmed->maximum && !confirmed->ecManual) {
                            try {
                                fresh = readTemperatures();
                                for (int channel = 0; channel < 2; ++channel)
                                    if (!confirmed->logical[channel].empty() && !fresh.processors[channel].celsius) available = false;
                            } catch (const PowerInterrupted&) { throw; }
                              catch (const std::exception&) { available = false; }
                        }
                        if (!available) {
                            if (Clock::now() - recoveryStarted < 10s) {
                                continue;
                            }
                            throw std::runtime_error("После пробуждения нет свежих температур; возвращаю штатное охлаждение.");
                        }
                        EcAccess::ObservationPause pause(ec); const EcAccess::Transaction transaction(ec);
                        verifyRecoveryOwner();
                        // Inactive tables may have been cleared by firmware. Snapshot
                        // the live bytes before constructing our own output again.
                        for (auto& [address, value] : expected) value = ec.readStable(address);
                        curves = confirmed->curves; logical = confirmed->logical;
                        linked = confirmed->linked; maximum = confirmed->maximum; ecManual = confirmed->ecManual;
                        lastRaw = {-1,-1}; level = {};
                        if (maximum) {
                            write(ec::kPerformanceMode, ec::kPerformanceBoostBit, ec::kPerformanceBoostBit);
                        }
                        else {
                            write(ec::kRamTableCommit, 0, ec::kRamTableCommitBit);
                            write(ec::kPerformanceMode, 0, ec::kPerformanceBoostBit);
                            if (!(ec.readStable(ec::kRamTableCapability) & ec::kRamTableSupportedBit))
                                throw std::runtime_error("После пробуждения EC не подтвердил поддержку RAM-управления.");
                            write(ec::kControl727, 0, ec::kControl727FanBit);
                            std::this_thread::sleep_for(50ms);
                            write(ec::kControl727, ec::kControl727FanBit, ec::kControl727FanBit);
                            write(ec::kCustomMode, ec::kCustomModeBit, ec::kCustomModeBit);
                            write(ec::kFanControlRespective, ecManual ? 0 : ec::kSplitModeBit, ec::kSplitModeBit);
                            reload = true;
                        }
                        log("FAN FLAGS restored: selected fan mode applied");
                    } else {
                        if (lease) {
                            if (touched) { verifyRecoveryOwner(); native(); }
                            release();
                            std::lock_guard lock(mutex); state = {}; state.revision = revision;
                            state.message = "Неподтверждённое управление отменено; штатное охлаждение восстановлено.";
                        }
                        restorePresence();
                        { std::lock_guard lock(mutex); state.busy = state.applying = false; }
                        finishPowerOperation();
                    }
                    recovering = false; presencePending.store(true);
                }
                if (command) {
                    revision = command->revision;
                    activeOperation = command->operation;
                    // A background failure may have completed after this
                    // request was admitted. Recheck its cleanup requirement at
                    // execution time, before acquiring or writing a new mode.
                    { std::lock_guard lock(mutex);
                      if(state.needsRestore&&command->kind!=Kind::Native&&command->kind!=Kind::DiagnosticPrepare)
                          throw std::runtime_error("Сначала завершите возврат штатного режима через Default."); }
                    if(command->kind==Kind::DiagnosticPrepare){
                        auto plan=std::make_shared<DiagnosticPlan>();
                        {std::lock_guard lock(mutex);plan->generation=userGeneration;plan->epoch=powerEpoch.load();
                            plan->commonEpoch=diagnosticFanEpoch();}
                        // This snapshot belongs to the one worker, not the CSV
                        // editor, last GUI highlight or an abandoned old session.
                        if(lease&&confirmed&&!ownershipUnknown){
                            try{
                                const EcAccess::Transaction transaction(ec);check(false);
                                // check(bool) deliberately verifies flags only.
                                // A diagnostic intent snapshot additionally
                                // proves its owned payload now, not its cache.
                                // Max owns only Fan Boost, never either table.
                                if(!maximum)for(int ch=0;ch<(ecManual?1:2);++ch){
                                    if(logical[ch].empty())continue;
                                    const auto base=ch==0?ec::kCpuTableBase:ec::kGpuTableBase;
                                    for(int i=0;i<48;++i){if(ecManual&&i==16)continue;
                                        const auto address=static_cast<std::uint16_t>(base+i);checkPower();
                                        if(!expected.contains(address)||ec.readStable(address)!=expected.at(address))
                                            throw ExternalChange("Diagnostic FAN snapshot: owned RAM payload changed externally");
                                    }
                                }
                                check(false);plan->previous=*confirmed;
                                plan->previousMode=confirmed->maximum?"Max":confirmed->ecManual?"Manual":"Manual online";
                            }catch(const PowerInterrupted&){throw;}
                             catch(const std::exception& e){
                                // The requested Default may still proceed, but
                                // no unproven/foreign payload becomes a plan.
                                plan->previous.reset();plan->previousMode="Unconfirmed";
                                log(std::string("DIAGNOSTIC FAN snapshot unavailable; old intent will not replay: ")+e.what());
                            }
                        }else{
                            const auto actual=readNativeFanFlags(ec);checkPower();
                            plan->previousMode=actual.defaultMode?"Default":"Unconfirmed";
                        }
                        command->diagnosticPlan=std::move(plan);command->kind=Kind::Native;
                        log("DIAGNOSTIC FAN snapshot: previous="+command->diagnosticPlan->previousMode+"; confirmed worker intent only");
                    }else if(command->kind==Kind::DiagnosticRestore){
                        const auto& plan=*command->diagnosticPlan;
                        diagnosticDefaultCheck(plan,plan.previous.has_value());
                        if(!plan.previous){
                            const bool retained=plan.previousMode=="Default";
                            {std::lock_guard lock(mutex);checkPower();
                                if(sequence==revision){state={};state.revision=revision;state.observedDefault=true;
                                    state.message=retained?"Штатный режим CPU/GPU подтверждён EC.":
                                        "Прежний режим FANs не подтверждён; оставлен Default.";}}
                            activeOperation.reset();command->diagnosticRestored->set_value({false,retained,
                                retained?"Default retained; no FANs replay required":"Previous FANs intent was not confirmed/owned; Default retained",false});
                            continue;
                        }
                        const auto& previous=*plan.previous;FanCurveProfiles saved;
                        saved.ecManual=previous.ecManual;saved.linked=previous.linked;saved.ecManualCurve=previous.curves[0];
                        saved.curves[static_cast<std::size_t>(FanCurvePreset::Manual)]=previous.curves;
                        for(std::size_t ch=0;ch<2;++ch)saved.selected[ch]=previous.maximum?FanCurvePreset::Max:
                            previous.logical[ch].empty()?FanCurvePreset::Default:FanCurvePreset::Manual;
                        command->savedProfiles=std::move(saved);
                        command->kind=previous.maximum?Kind::Maximum:previous.ecManual?Kind::EcManual:Kind::SavedProfiles;
                        command->diagnosticAdmitted=true;
                        log("DIAGNOSTIC FAN restore admitted: previous="+plan.previousMode+"; generation/epoch/fresh Default verified");
                    }
                    startupConfirmationPending = command->savedProfiles.has_value();
                    if (startupConfirmationPending) {
                        const auto& saved = *command->savedProfiles;
                        log(std::format("CONFIG FAN requested: CPU={} GPU={} linked={}",
                            kFanCurvePresetNames[static_cast<std::size_t>(saved.currentPreset(0))],
                            kFanCurvePresetNames[static_cast<std::size_t>(saved.currentPreset(1))], saved.linked));
                        recordPowerAction("fan_config", "requested", -1);
                    }
                    if(command->kind==Kind::MaximumProbe){
                        // Execute inside the same worker as Manual/online. A
                        // separate writer would race its ownership watchdog.
                        bool captured=false;std::map<std::uint16_t,std::uint8_t> original;
                        const std::array probeFields{
                            std::pair{ec::kRamTableCommit,ec::kRamTableCommitBit},
                            std::pair{ec::kCustomMode,ec::kCustomModeBit},
                            std::pair{ec::kControl727,ec::kControl727FanBit},
                            std::pair{ec::kFanControlRespective,ec::kSplitModeBit}};
                        const auto result=measureMaximumFans("X5SP4NAG · EC Fan Boost",[&]{
                            checkPower();const EcAccess::Transaction transaction(ec);
                            original[ec::kPerformanceMode]=ec.readStable(ec::kPerformanceMode);
                            for(auto [a,mask]:probeFields){(void)mask;original[a]=ec.readStable(a);}
                            captured=true;
                            if(!(original[ec::kPerformanceMode]&ec::kPerformanceBoostBit))
                                (void)ec.updateBitsVerified(ec::kPerformanceMode,ec::kPerformanceBoostBit,ec::kPerformanceBoostBit);
                        },[&]{
                            checkPower();FanMaximumSample sample;
                            if(captured&&!(ec.readStable(ec::kPerformanceMode)&ec::kPerformanceBoostBit))
                                throw ExternalChange("Fan Boost изменён вне пробы");
                            // This is an admitted active probe, not optional UI
                            // polling. Wait on the same serialized EC gate;
                            // another reader's ObservationPause must not abort
                            // the 30 s hold. Power/cancel/ownership checks remain.
                            for(int ch=0;ch<2;++ch){const auto f=ec.readFanFeedback(ch?FanChannel::Gpu:FanChannel::Cpu,false);
                                sample.rpm[ch]=static_cast<int>(f.rpm);sample.percent[ch]=f.dutyPercent;}
                            checkPower();return sample;
                        },[&]{
                            if(!captured)return;checkPower();const EcAccess::Transaction transaction(ec);
                            for(auto [a,mask]:probeFields)if((ec.readStable(a)&mask)!=(original[a]&mask))
                                throw ExternalChange("Флаги вентиляторов изменены извне; прежнее состояние не перезаписано");
                            const auto mode=ec.readStable(ec::kPerformanceMode);
                            if((mode&~ec::kPerformanceBoostBit)!=(original[ec::kPerformanceMode]&~ec::kPerformanceBoostBit))
                                throw ExternalChange("Профиль EC изменён извне; прежний режим не перезаписан");
                            if((mode&ec::kPerformanceBoostBit)!=(original[ec::kPerformanceMode]&ec::kPerformanceBoostBit))
                                (void)ec.updateBitsVerified(ec::kPerformanceMode,ec::kPerformanceBoostBit,original[ec::kPerformanceMode]&ec::kPerformanceBoostBit);
                        },command->probeCancellation);
                        if(!result.restored){
                            if(interruptedByPower()){interrupted(command);continue;}
                            else{
                                fail(result.status,true,native,release,false,revision);
                                confirmed.reset();recovering=false;finishPowerOperation();activeOperation.reset();
                                logical={};maximum=false;lastRaw={-1,-1};reload=false;
                            }
                            command->probe->set_value(result);continue;
                        }
                        // A probe is not a mode-selection intent. Publish its
                        // verified restored mode, not request()'s stale progress.
                        FanControlStatus after;after.revision=revision;
                        {const EcAccess::Transaction transaction(ec);checkPower();
                            const auto flags=readNativeFanFlags(ec);
                            if(lease)check(false);
                            after.observedDefault=flags.defaultMode;
                            if(lease&&confirmed){after.owned=true;after.maximum=maximum;after.ecManual=ecManual;after.linked=linked;after.applied=curves;
                                for(int ch=0;ch<2;++ch){after.controlled[ch]=maximum||!logical[ch].empty();after.confirmed[ch]=after.controlled[ch];}}
                            after.message=flags.defaultMode?"Штатный режим CPU/GPU подтверждён EC.":
                                "Проба Max завершена; прежний режим FANs подтверждён.";}
                        {std::lock_guard lock(mutex);checkPower();if(sequence==revision)state=std::move(after);}
                        log("MAXIMUM PROBE: "+result.status+"; original Fan Boost restored; tables/APU untouched");
                        // Publish completion after releasing the operation. The
                        // dump immediately reads the post-probe EC snapshot.
                        activeOperation.reset();command->probe->set_value(result);continue;
                    }
                    if (command->kind == Kind::Performance) {
                        // SETs owns only APU profile fields. Never re-arm fan gates,
                        // clear Fan Boost, replace tables or change fan ownership here.
                        try {
                            const auto change = command->customBase?selectWujieCustomBase(ec,*command->customBase):command->customPower?applyCustomPowerLimits(ec,*command->customPower):selectEcPerformance(ec, command->channel);
                            if (lease) expected[ec::kPerformanceMode] = change.after;
                            if (confirmed) confirmed->mode = change.after;
                            { std::lock_guard lock(mutex);
                              state.revision = revision; state.busy = state.applying = false;
                              state.message = ecManual ? "EC_manual: CPU=GPU; общая кривая CPU, управление EC" :
                                  maximum ? "Max CPU/GPU" : linked ? "CPU=GPU" : "CPU≠GPU";
                            }
                            command->performance->set_value(change);
                            log(command->customPower?"CUSTOM POWER confirmed: own CPU base751=10 and overrides; fan flags/tables untouched":"PERFORMANCE confirmed: APU profile only; fan flags and tables untouched");
                        } catch (...) {
                            command->performance->set_exception(std::current_exception());
                            { std::lock_guard lock(mutex); state.busy = state.applying = false; }
                            log("PERFORMANCE failed: fan mode retained; no fan restoration requested");
                        }
                        activeOperation.reset();
                        finishPowerOperation();
                        continue;
                    } else {
                    // An unchanged explicit selection is a no-op, not a table activation.
                    bool unchanged = false;
                    if (command->kind == Kind::Native && !lease && !maximum && !ecManual) {
                        unchanged = true;
                        for (const auto [address,mask] : {
                            std::pair{ec::kRamTableCommit,ec::kRamTableCommitBit},
                            std::pair{ec::kCustomMode,ec::kCustomModeBit},
                            std::pair{ec::kControl727,ec::kControl727FanBit},
                            std::pair{ec::kPerformanceMode,ec::kPerformanceBoostBit}})
                            unchanged &= (ec.readStable(address)&mask)==0;
                    } else if (lease && confirmed && !recovering) {
                        if (command->kind == Kind::Maximum) unchanged = maximum;
                        else if (command->savedProfiles) {
                            const auto& requested=*command->savedProfiles;
                            const bool wantsMaximum=requested.currentPreset(0)==FanCurvePreset::Max;
                            unchanged = maximum==wantsMaximum && ecManual==(command->kind==Kind::EcManual) && linked==requested.linked;
                            for (int channel=0;channel<2;++channel) {
                                const bool wantsCurve=!wantsMaximum && requested.currentPreset(channel)!=FanCurvePreset::Default;
                                unchanged &= wantsCurve==!logical[channel].empty();
                                if (wantsCurve) unchanged &= curves[channel]==requested.current(channel) &&
                                    tableCacheValid[ecManual?0:channel];
                            }
                        } else if (command->kind == Kind::Curve) {
                            unchanged = !maximum && !ecManual && linked==command->linked;
                            for (int channel=0;channel<2;++channel) if (linked || channel==command->channel)
                                unchanged &= curves[channel]==command->curve && tableCacheValid[channel];
                        }
                        if (unchanged && !flagsMatch()) { unchanged=false; tableCacheValid.fill(false); lastRaw={-1,-1}; }
                        if(unchanged&&command->kind==Kind::EcManual){
                            // A user clicking autonomous Manual asks about the
                            // live CPU payload, not our previous online/Max cache.
                            // Read no GPU table and never own firmware Down[0].
                            try{
                                const EcAccess::Transaction transaction(ec);
                                const auto wanted=buildAutonomousFanTable(points(command->savedProfiles->current(0)));
                                for(int i=0;i<48;++i){
                                    const auto address=static_cast<std::uint16_t>(ec::kCpuTableBase+i);
                                    const auto actual=ec.readStable(address);
                                    if(i!=16)unchanged&=actual==wanted[i];
                                }
                                check(false);
                            }catch(const PowerInterrupted&){throw;}
                             catch(const ExternalChange&){throw;}
                             catch(const FanFlagsUnavailable&){throw;}
                             catch(const std::exception& error){
                                // No write has occurred. Unknown table readback
                                // must not trigger Default over the retained mode.
                                throw FanFlagsUnavailable(std::string("Manual CPU table readback unavailable: ")+error.what());
                            }
                            if(!unchanged)tableCacheValid[0]=false;
                            log(unchanged?"EC_MANUAL repeat: fresh CPU table and owned flags match; no re-arm; execution not measured":
                                "EC_MANUAL repeat: fresh CPU payload differs; explicit command will compare/update it");
                        }
                    }
                    if (unchanged) {
                        if(command->kind==Kind::Native&&command->resetApplicationConsumers){
                            EcAccess::ObservationPause pause(ec);const EcAccess::Transaction transaction(ec);
                            resetApplicationControl(ec,disableCopilot,checkPower);presencePending.store(false);
                        }
                        if(command->kind==Kind::Native&&status().needsRestore){
                            // Default's existing flag readback succeeded. Retry
                            // the original failed service restoration before
                            // lifting the guard; do not write FAN tables/flags.
                            release();
                            std::lock_guard lock(mutex);state.needsRestore=false;
                        }
                        { std::lock_guard lock(mutex);
                          // Same publication order as the full apply commit:
                          // a suspend after readback cannot complete this request.
                          checkPower();
                          state.revision=revision; state.busy=state.applying=false;
                          state.error=false;state.observedDefault=!lease;
                          // A successful explicit read-only retry can restore
                          // mode confirmation after an unknown-owner observation.
                          // This confirms readback only, never physical execution.
                          state.confirmed=state.controlled;ownershipUnknown=false;
                          state.message=ecManual ? "Manual: уже применён" : maximum ? "Max: уже применён" :
                              !lease ? "Default: уже применён" : "Manual online: уже применён";
                        }
                        if (command->native) command->native->set_value();
                        startupConfirmationPending=false; activeOperation.reset();
                        log(command->resetApplicationConsumers?
                            "DEFAULT confirmed: consumers reset, Copilot Off saved, APP released; tables/PL untouched":
                            "CONFIG FAN confirmed: unchanged selection; no table or activation writes");
                        preparedDiagnostic(*command);restoredDiagnostic(command);
                        continue;
                    }
                    // Validate fresh data before acquiring/changing fan ownership.
                    const bool savedMaximum = command->savedProfiles &&
                        command->savedProfiles->currentPreset(0) == FanCurvePreset::Max;
                    if (command->kind == Kind::Curve || (command->savedProfiles && !savedMaximum && command->kind != Kind::Native && command->kind != Kind::EcManual)) {
                        const auto deadline = Clock::now() + 30s;
                        for (;;) {
                            checkPower();
                            if (token.stop_requested()) throw std::runtime_error("Запуск управления вентиляторами отменён при выходе.");
                            const auto sample = readTemperatures();
                            bool ready = true;
                            for (int channel = 0; channel < 2; ++channel) {
                                const bool required = command->savedProfiles
                                    ? command->savedProfiles->currentPreset(channel) != FanCurvePreset::Default
                                    : command->linked || channel == command->channel;
                                if (required && !sample.processors[channel].celsius) ready = false;
                            }
                            if (ready) break;
                            if (!command->savedProfiles || Clock::now() >= deadline)
                                throw std::runtime_error("Нет свежей температуры AMD ADL для выбранной кривой.");
                            std::this_thread::sleep_for(250ms);
                        }
                    }
                    const bool onlineRequested=command->kind==Kind::Curve||command->kind==Kind::Link||
                        (command->kind==Kind::SavedProfiles&&!savedMaximum&&command->savedProfiles&&
                         (command->savedProfiles->currentPreset(0)!=FanCurvePreset::Default||command->savedProfiles->currentPreset(1)!=FanCurvePreset::Default));
                    begin(command->kind == Kind::Maximum || command->kind == Kind::Native || savedMaximum,!command->diagnosticRestored,onlineRequested);
                    // Recheck after taking our process/channel lease, still
                    // before any writer. Automatic replay never pauses OEM.
                    if(command->diagnosticRestored)diagnosticDefaultCheck(*command->diagnosticPlan);
                    if(command->diagnosticRestored){automaticRestoreWrites=true;command->diagnosticPlan->writePhaseEntered.store(true);}
                    if (command->kind == Kind::EcManual) {
                        tableCacheValid[1]=false;
                        for (auto it=expected.lower_bound(ec::kGpuTableBase); it!=expected.end() && it->first<ec::kTableEnd;)
                            it=expected.erase(it);
                    }
                    if (command->adoptExisting && command->savedProfiles &&
                        (command->kind == Kind::EcManual || savedMaximum)) {
                        const auto& saved = *command->savedProfiles;
                        const EcAccess::Transaction transaction(ec);
                        bool matches = (ec.readStable(ec::kPerformanceMode) & ec::kPerformanceBoostBit) != 0;
                        if (!savedMaximum) {
                            matches = !matches && (ec.readStable(ec::kRamTableCommit) & ec::kRamTableCommitBit) &&
                                (ec.readStable(ec::kCustomMode) & ec::kCustomModeBit) &&
                                (ec.readStable(ec::kControl727) & ec::kControl727FanBit) &&
                                (ec.readStable(ec::kAppPresence) & ec::kAppPresenceBit) &&
                                !(ec.readStable(ec::kFanControlRespective) & ec::kSplitModeBit);
                            const auto table = buildAutonomousFanTable(points(saved.current(0)));
                            for (int i=0; i<48; ++i) {
                                const auto address=static_cast<std::uint16_t>(ec::kCpuTableBase+i);
                                const auto value=ec.readStable(address);
                                expected[address]=value;
                                if (i!=16) matches &= value==table[i];
                            }
                        }
                        if (matches) {
                            maximum = savedMaximum; ecManual = !savedMaximum; linked = saved.linked; reload = false;
                            tableCacheValid[0]=!savedMaximum;
                            for (int channel=0; channel<2; ++channel) {
                                curves[channel] = saved.current(channel);
                                logical[channel] = maximum ? std::vector<FanCurvePoint>{} : points(curves[channel]);
                            }
                            confirmed = Intent{curves,logical,linked,maximum,ecManual,ec.readStable(ec::kPerformanceMode)};
                            { std::lock_guard lock(mutex);
                              state = {}; state.revision = revision; state.owned = true; state.maximum = maximum;
                              state.ecManual = ecManual; state.linked = linked; state.applied = curves;
                              state.controlled.fill(true); state.confirmed.fill(true);
                              state.message = maximum ? "Max: существующий флаг EC подтверждён" : "Manual: существующая CPU-таблица EC подтверждена";
                            }
                            startupConfirmationPending = false; activeOperation.reset();
                            log("CONFIG FAN adopted: existing autonomous EC state matches; no table or fan flag writes");
                            continue;
                        }
                        log("CONFIG FAN mismatch: applying saved autonomous mode");
                    }
                    onlineReconfigure = lease && confirmed && !recovering && !maximum && !ecManual &&
                        (command->kind==Kind::Curve || command->kind==Kind::SavedProfiles || command->kind==Kind::Link);
                    if (onlineReconfigure) {
                        for (int channel=0;channel<2;++channel) {
                            const bool required=command->savedProfiles ? command->savedProfiles->currentPreset(channel)!=FanCurvePreset::Default :
                                command->kind==Kind::Link || command->linked || channel==command->channel || !logical[channel].empty();
                            if (required && !tableCacheValid[channel]) onlineReconfigure=false;
                        }
                        if (onlineReconfigure && !flagsMatch()) {
                            onlineReconfigure=false; tableCacheValid.fill(false); lastRaw={-1,-1};
                        }
                    }
                    if (onlineReconfigure) log("ONLINE reconfigure: verified table cache; commit and APP presence retained");
                    // Once a new mode starts changing EC, the old mode must
                    // never be replayed while the new PWM is still settling.
                    confirmed.reset();
                    if (command->kind != Kind::Link) ecManual = command->kind == Kind::EcManual;
                    if (command->kind == Kind::Native) {
                        native(command->resetApplicationConsumers);
                        if(command->resetApplicationConsumers)presencePending.store(false);
                        if (command->savedProfiles) {
                            linked = command->savedProfiles->linked;
                        }
                        release(); confirmed.reset();
                        {std::lock_guard lock(mutex);checkPower();
                            state = {}; state.revision = revision; state.linked = linked;
                            state.observedDefault=true;
                            state.message = "Штатный режим CPU/GPU подтверждён EC.";}
                        if (command->native) command->native->set_value();
                        activeOperation.reset();
                        log("DEFAULT confirmed");preparedDiagnostic(*command);continue;
                    }
                    if (command->kind == Kind::Maximum || savedMaximum) {
                        // Max is ONE masked Fan Boost write (0x751.6). Keep the
                        // APU preset, existing RAM tables, commit, split and APP presence.
                        if (command->savedProfiles) linked = command->savedProfiles->linked;
                        { const EcAccess::Transaction transaction(ec);
                            write(ec::kPerformanceMode, ec::kPerformanceBoostBit, ec::kPerformanceBoostBit);
                        }
                        maximum = true; ecManual = false; reload = false;
                        log("MAX FanBoost flag enabled; one flag only, tables and other fan gates retained");
                    } else if (command->kind == Kind::Link) {
                        if (ecManual) throw std::runtime_error("EC_manual использует общую кривую CPU=GPU.");
                        linked = command->linked;
                        if (logical[0].empty() && logical[1].empty()) {
                            const auto actual = readNativeFanState(ec);
                            if (actual.active)
                                throw std::runtime_error("RAM-кривые оставлены прежней сессией. Сначала нажмите Min или Manual; общий аппаратный режим не включён.");
                            { const EcAccess::Transaction transaction(ec);
                                write(ec::kFanControlRespective, linked ? 0 : ec::kSplitModeBit, ec::kSplitModeBit);
                            }
                            std::lock_guard lock(mutex);
                            state.revision = revision; state.linked = linked; state.busy = false; state.error = false;
                            state.applying = false;
                            state.message = linked ? "CPU=GPU: общий режим EC подтверждён." : "CPU≠GPU: раздельный режим EC подтверждён.";
                            state.owned = true;
                            activeOperation.reset(); log(state.message); continue;
                        }
                        if (linked) {
                            const int source = logical[0].empty() ? 1 : 0;
                            logical[0] = logical[1] = logical[source]; curves[0] = curves[1] = curves[source];
                        }
                        reload = true; lastRaw = {-1,-1}; level = {};
                    } else if (command->savedProfiles) {
                        const auto& saved = *command->savedProfiles;
                        if (maximum) native();
                        maximum = false; linked = saved.linked;
                        for (int channel = 0; channel < 2; ++channel) {
                            curves[channel] = saved.current(channel);
                            logical[channel] = saved.currentPreset(channel) == FanCurvePreset::Default
                                ? std::vector<FanCurvePoint>{} : points(curves[channel]);
                            level[channel] = 0; lastRaw[channel] = -1;
                        }
                        reload = true;
                    } else {
                        if (maximum) native();
                        maximum = false; linked = command->linked;
                        for (int channel = 0; channel < 2; ++channel) if (linked || channel == command->channel) {
                            logical[channel] = points(command->curve); curves[channel] = command->curve;
                            level[channel] = 0; lastRaw[channel] = -1;
                        }
                        reload = true;
                    }
                    if (command->kind == Kind::EcManual) ecManual = true;
                    if (onlineReconfigure) reload=false;
                    if (!maximum && !onlineReconfigure) {
                        EcAccess::ObservationPause pause(ec); const EcAccess::Transaction transaction(ec);
                        const int mode = ec.readStable(ec::kPerformanceMode) & ~ec::kPerformanceBoostBit;
                        if (mode != 0 && mode != 0x10 && mode != 0x80 && mode != 0xA0)
                            throw std::runtime_error("Выберите Office, Balanced или Turbo перед применением кривой.");
                        if ((ec.readStable(ec::kRamTableCapability) & ec::kRamTableSupportedBit) == 0)
                            throw std::runtime_error("EC не сообщил поддержку RAM-управления.");
                        write(ec::kPerformanceMode, 0, ec::kPerformanceBoostBit);
                        write(ec::kRamTableCommit, 0, ec::kRamTableCommitBit);
                        write(ec::kControl727, 0, ec::kControl727FanBit);
                        std::this_thread::sleep_for(50ms);
                        write(ec::kControl727, ec::kControl727FanBit, ec::kControl727FanBit);
                        write(ec::kCustomMode, ec::kCustomModeBit, ec::kCustomModeBit);
                        // Same byte also contains keyboard pulsation in bits 2:0.
                        // Hardware common is ONLY autonomous Manual. Software linked
                        // online curves still need two independent hardware channels.
                        write(ec::kFanControlRespective, ecManual ? 0 : ec::kSplitModeBit, ec::kSplitModeBit);
                    }
                    }
                }
                if (!maximum && logical[0].empty() && logical[1].empty()) {if(!ecManual)ownershipSafety.reset();continue;}
                MonitoringSnapshot sample;
                std::array<int,2> target{};
                if (ecManual) {
                    if (reload) {
                        EcAccess::ObservationPause pause(ec); const EcAccess::Transaction transaction(ec);
                        const auto table = buildAutonomousFanTable(logical[0]);
                        std::array<std::uint8_t,48> actual{};
                        const auto readStart = Clock::now();
                        // Explicit Manual/recovery always compares live CPU
                        // bytes after opening RAM writes. Neither a previous
                        // Max/online cache nor a pre-arm adoption snapshot can
                        // stand in for this read. The GPU table stays untouched.
                        for (int i=0;i<48;++i) {
                            const auto address=static_cast<std::uint16_t>(ec::kCpuTableBase+i);
                            actual[i]=ec.readStable(address); expected[address]=actual[i];
                        }
                        const auto readMs=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-readStart).count();
                        const auto writeStart=Clock::now();
                        int changed{};
                        for (int i=0;i<48;++i) {
                            if (i==16) continue; // Firmware-owned Down[0].
                            const auto address=static_cast<std::uint16_t>(ec::kCpuTableBase+i);
                            if (actual[i]!=table[i]) {
                                touched=true; ec.writeVerified(address,table[i]); ++changed;
                            }
                            expected[address]=table[i];
                        }
                        const auto writeMs=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-writeStart).count();
                        tableCacheValid[0]=true;
                        log(std::format("EC_MANUAL CPU table: read_compare_ms={} changed_bytes={} write_verify_ms={}; GPU table untouched",readMs,changed,writeMs));
                        for (const auto& point : logical[0])
                            log(std::format("EC_MANUAL TABLE channel=0 UpT={} DownT={} PWM={}",point.temperature,point.temperature-3,point.duty));
                        write(ec::kRamTableCommit, ec::kRamTableCommitBit, ec::kRamTableCommitBit);
                        write(ec::kAppPresence, 0, ec::kAppPresenceBit);
                        std::this_thread::sleep_for(300ms);
                        write(ec::kAppPresence, ec::kAppPresenceBit, ec::kAppPresenceBit);
                        check(false);
                        reload = false;
                        log("EC_MANUAL armed: CPU=GPU; CPU table compared/updated; GPU table untouched; host PWM loop disabled");
                        recordPowerAction("ec_manual", "tables_verified", -1);
                    }
                } else {
                if (!maximum) {
                    sample = readTemperatures();
                    for (int channel = 0; channel < 2; ++channel)
                        if (!logical[channel].empty() && !sample.processors[channel].celsius)
                            throw std::runtime_error("Температура AMD ADL недоступна; возвращаю штатный режим EC.");
                }
                { EcAccess::ObservationPause pause(ec); const EcAccess::Transaction transaction(ec);
                    // Check even when the target is unchanged. A cached table
                    // never grants ownership after a foreign gate/boost change.
                    check(true);
                    bool protection{};
                    for (const auto& processor : sample.processors)
                        protection |= processor.celsius && *processor.celsius >= 85;
                    for (int channel = 0; channel < 2; ++channel) {
                        if (maximum) { target[channel] = 100; continue; }
                        const auto& curve = logical[channel]; if (curve.empty()) continue;
                        const double temperature = linked ? std::max(*sample.processors[0].celsius,*sample.processors[1].celsius)
                            : *sample.processors[channel].celsius;
                        level[channel]=fanCurveLevel(curve,level[channel],temperature);
                        target[channel] = protection ? 100 : curve[level[channel]].duty;
                        const int raw = target[channel] * 2;
                        if (lastRaw[channel] == raw) continue;
                        const std::uint16_t base = channel == 0 ? ec::kCpuTableBase : ec::kGpuTableBase;
                        const bool initial = lastRaw[channel] < 0;
                        const auto readStart = Clock::now();
                        std::array<std::uint8_t,48> actual{};
                        const bool cached = initial && tableCacheValid[channel];
                        if (initial) for (int i=0;i<48;++i) {
                            const auto address=static_cast<std::uint16_t>(base+i);
                            actual[i]=cached ? expected.at(address) : ec.readStable(address);
                            expected[address]=actual[i];
                        }
                        const auto readMs=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-readStart).count();
                        const auto writeStart=Clock::now(); int changed{};
                        if (initial) {
                            const auto table=output(raw,channel);
                            for (int i=0;i<48;++i) {
                                const auto address=static_cast<std::uint16_t>(base+i);
                                if (actual[i]!=table[i]) { touched=true; ec.writeVerified(address,table[i]); ++changed; }
                                expected[address]=table[i];
                            }
                        } else for (int i=0;i<(channel==0 ? 6 : 16);++i) {
                            const auto address=static_cast<std::uint16_t>(base+32+i);
                            const auto desired=static_cast<std::uint8_t>(raw);
                            if (expected.at(address)!=desired) { touched=true; ec.writeVerified(address,desired); ++changed; }
                            expected[address]=desired;
                        }
                        const auto writeMs=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-writeStart).count();
                        tableCacheValid[channel]=true;
                        log(std::format("ONLINE TABLE channel={} initial={} cached={} read_compare_ms={} changed_bytes={} write_verify_ms={} linked={}",channel,initial,cached,readMs,changed,writeMs,linked));
                        lastRaw[channel] = raw;
                        log(std::format("TARGET {} temperature={}C duty={}% linked={}",channel,*sample.processors[channel].celsius,target[channel],linked));
                    }
                    if (!maximum) {
                        // Ownership flags are applied once per explicit command/recovery,
                        // never reasserted by an ordinary temperature/telemetry cycle.
                        if (reload) write(ec::kRamTableCommit, ec::kRamTableCommitBit, ec::kRamTableCommitBit);
                        const bool verifyMode = reload;
                        if (reload) {
                            write(ec::kAppPresence, 0, ec::kAppPresenceBit); std::this_thread::sleep_for(300ms);
                            write(ec::kAppPresence, ec::kAppPresenceBit, ec::kAppPresenceBit);
                            // Each changed byte was verified on write; no second full table scan.
                            reload = false;
                        }
                        if (verifyMode) {
                            check(false); // Verify mode flags only, without reading either table again.
                            log(linked ? "ONLINE armed: common software curve; both EC channels split and updated" :
                                         "ONLINE armed: separate software curves; both EC channels split and updated");
                        }
                    }
                }
                }
                if (performanceResult) {
                    if (ec.readStable(ec::kPerformanceMode) != performanceResult->after)
                        throw std::runtime_error("EC не сохранил пресет после обновления RAM-управления.");
                    activeOperation.reset();
                    log("PERFORMANCE confirmed; Manual curves and fan ownership preserved");
                }
                FanControlStatus next;
                next.revision = revision; next.ecManual = ecManual; next.owned = true; next.maximum = maximum; next.linked = linked; next.applied = curves;
                // RPM/PWM collection never checks or restores configuration.
                for (int channel = 0; channel < 2; ++channel) {
                    next.controlled[channel] = maximum || !logical[channel].empty(); if (!next.controlled[channel]) continue;
                    next.target[channel] = ecManual ? std::nullopt : std::optional<int>(target[channel]);
                    try {
                        const auto reading = ec.readFanFeedback(channel == 0 ? FanChannel::Cpu : FanChannel::Gpu);
                        checkPower();
                        next.actual[channel] = reading.dutyPercent; next.rpm[channel] = reading.rpm;
                        next.confirmed[channel] = true; // Mode confirmed by flags, readings are telemetry only.
                    } catch (const PowerInterrupted&) { throw; }
                      catch (const std::exception& error) {
                        const auto previous = status();
                        next.actual[channel] = previous.actual[channel]; next.rpm[channel] = previous.rpm[channel];
                        next.confirmed[channel] = true;
                        log(std::string("FAN telemetry unavailable; mode retained: ") + error.what());
                    }
                }
                next.busy = false;
                checkPower();
                confirmed = Intent{curves, logical, linked, maximum, ecManual, expected.at(ec::kPerformanceMode)};
                next.applying = false;
                next.message = ecManual ? "EC_manual: CPU=GPU; общая кривая CPU, управление EC" : maximum ? "Max CPU/GPU" : linked ? "CPU=GPU: общая кривая по max(CPU, GPU)" : "CPU≠GPU: раздельные кривые";
                next.message += " · режим подтверждён флагами EC.";
                {
                    // Publish readiness before releasing the switch gate. A
                    // queued click must not see the previous applying state.
                    std::lock_guard lock(mutex); checkPower();
                    if(sequence==revision)state=next;
                    else log(std::format("FAN STATUS superseded: observed_revision={} requested_revision={}; newer applying state retained",revision,sequence));
                    activeOperation.reset();
                }
                if(ecManual||maximum)ownershipSafety.reset();
                // Serialise EC application, not the physical fan ramp. Default
                // and the other presets remain usable while feedback settles.
                finishPowerOperation();
                if(command&&command->kind==Kind::EcManual&&command->native)command->native->set_value();
                if (startupConfirmationPending) {
                    log("CONFIG FAN confirmed: saved fan profiles active; EC mode flags verified");
                    recordPowerAction("fan_config", "confirmed", -1);
                    startupConfirmationPending = false;
                }
                log(std::format("FEEDBACK revision={} CPU target={} actual={} rpm={} GPU target={} actual={} rpm={} mode_readback_confirmed={} physical_execution=unverified",
                    revision,next.target[0].value_or(-1),next.actual[0].value_or(-1),next.rpm[0].value_or(-1),
                    next.target[1].value_or(-1),next.actual[1].value_or(-1),next.rpm[1].value_or(-1),true));
                if (performanceResult) command->performance->set_value(*performanceResult);
                restoredDiagnostic(command);
            } catch (const DiagnosticRestoreSkipped& error) {
                // Admission failed before writes: no Default/old snapshot replay
                // over foreign or unreadable flags. Release only our new lease.
                if(command&&command->diagnosticRestored){
                    if(lease&&!touched)try{release();}catch(const std::exception& e){log(std::string("DIAGNOSTIC lease release: ")+e.what());}
                    {std::lock_guard lock(mutex);if(sequence==revision){state.busy=state.applying=false;state.error=!error.superseded;
                        state.confirmed={};state.observedDefault.reset();state.message=error.what();}}
                    activeOperation.reset();command->diagnosticRestored->set_value({false,false,error.what(),error.superseded});
                    log(std::string("DIAGNOSTIC FAN restore skipped: ")+error.what());
                }else throw;
            } catch (const PowerInterrupted&) {
                startupConfirmationPending = false;
                interrupted(command);
            } catch (const FanFlagsUnavailable& error) {
                if (interruptedByPower()) { interrupted(command); continue; }
                if (!command && confirmed && !recovering) {
                    // No writes preceded the per-cycle ownership check. Retain
                    // intent/lease for a read-only retry, not a reactivation.
                    log(std::string("FAN FLAGS unavailable; writes paused: ") + error.what());
                    ownershipUnknown = true;
                    { std::lock_guard lock(mutex); state.confirmed = {}; state.observedDefault.reset();
                      if(sequence==revision){state.applying = state.busy = false;
                          state.message = std::string("Режим FANs не подтверждён; запись приостановлена: ") + error.what();} }
                    activeOperation.reset();
                    continue;
                }
                // An explicit command with an unreadable owner must fail closed;
                // do not issue Default/rollback over potentially foreign state.
                startupConfirmationPending = false;
                fail(error.what(), true, native, release, false, revision);
                confirmed.reset(); recovering = false; finishPowerOperation();
                activeOperation.reset(); presencePending.store(false);
                logical = {}; maximum = false; ecManual = false; lastRaw = {-1,-1}; reload = false;
                commandFailed(command, std::make_exception_ptr(std::runtime_error(status().message)));
            } catch (const ExternalChange& error) {
                if (interruptedByPower()) { interrupted(command); continue; }
                if (startupConfirmationPending) { log(std::string("CONFIG FAN failed: ")+error.what()); startupConfirmationPending = false; }
                fail(error.what(), true, native, release, touched, revision);
                confirmed.reset(); recovering = false; finishPowerOperation();
                activeOperation.reset(); presencePending.store(false);
                logical = {}; maximum = false; ecManual = false; lastRaw = {-1,-1}; reload = false;
                const auto failure = std::make_exception_ptr(std::runtime_error(status().message));
                commandFailed(command,failure);
            } catch (const std::exception& error) {
                if (interruptedByPower()) {
                    interrupted(command);
                    continue;
                }
                if (startupConfirmationPending) { log(std::string("CONFIG FAN failed: ")+error.what()); startupConfirmationPending = false; }
                if (lease && touched) {
                    // Recovery does not waive cleanup ownership. Even if sleep
                    // was confirmed, an error cannot grant permission to clear
                    // gates whose current masks are foreign or unreadable.
                    try { check(false); }
                    catch (const ExternalChange&) { external = true; }
                    catch (const std::exception& ownershipError) {
                        // An unreadable owner is not permission to roll back its
                        // gates. Fail without EC writes when ownership is unknown.
                        external = true;
                        log(std::string("FAN cleanup skipped: ownership unknown: ") + ownershipError.what());
                    }
                }
                fail(error.what(), external, native, release, touched && !token.stop_requested(), revision);
                confirmed.reset(); recovering = false; finishPowerOperation();
                if(external)presencePending.store(false);
                activeOperation.reset();
                logical = {}; maximum = false; lastRaw = {-1,-1}; reload = false;
                const auto failure = std::make_exception_ptr(std::runtime_error(status().message));
                commandFailed(command,failure);
            }
        }
        // Exiting is not a fan-mode command. Keep the last committed table,
        // Fan Boost, split/common flag and APP presence exactly as applied.
        // Release only process ownership / service resources; no EC I/O here.
        try { release(); }
        catch (const std::exception& error) { log(std::string("EXIT resource release failed: ") + error.what()); }
        log("EXIT: EC state retained; no table clearing or fan flag writes");
        environment.reset(); // Sensor resources are released on their worker thread.
    }
    template<class Native, class Release>
    void fail(std::string error, bool external, Native native, Release release, bool touched, std::uint64_t revision) {
        bool needsRestore = status().needsRestore;
        if (touched && !external) {
            try { native(); error += " Штатный режим восстановлен."; }
            catch (const std::exception& failure) { needsRestore = true; error += " Возврат EC не подтверждён: "+std::string(failure.what()); }
        }
        try { release(); } catch (const std::exception& failure) { needsRestore = true; error += " GCUBridge: "+std::string(failure.what()); }
        log("ERROR "+error);
        std::lock_guard lock(mutex);
        if(pending&&pending->revision>revision){
            // The failed old mode is no longer confirmed, but its completion
            // cannot clear the progress of an already accepted newer command.
            // A required cleanup is rechecked before that command can write.
            const auto progress=state.message;
            state={};state.revision=revision;state.needsRestore=needsRestore;
            state.busy=state.applying=true;state.message=progress;
        }else{state = {}; state.revision = revision; state.needsRestore = needsRestore; state.error = true; state.message = std::move(error);}
    }
};

FanController::FanController(EcAccess& access, std::unique_ptr<FanControlEnvironment> environment)
    : impl_(std::make_shared<Impl>(access, std::move(environment))) {}
FanController::~FanController() = default;
void FanController::apply(int channel,const FanCurveTable& curve,bool linked) {
    if (channel < 0 || channel > 1) throw std::invalid_argument("Неизвестный канал вентилятора.");
    impl_->request({Impl::Kind::Curve,channel,curve,linked});
}
void FanController::restoreSavedProfiles(const FanCurveProfiles& profiles, std::shared_ptr<DeviceOperation> operation, bool adoptExisting) {
    if (profiles.currentPreset(0) == FanCurvePreset::Default && profiles.currentPreset(1) == FanCurvePreset::Default) {
        Impl::Request request{Impl::Kind::Native}; request.operation = std::move(operation);
        request.savedProfiles = profiles;
        impl_->request(std::move(request));
        return;
    }
    Impl::Request request{profiles.ecManual ? Impl::Kind::EcManual : Impl::Kind::SavedProfiles};
    request.adoptExisting = adoptExisting;
    request.savedProfiles = profiles;
    request.operation = std::move(operation);
    impl_->request(std::move(request));
}
void FanController::ecManual(const FanCurveTable& cpu, const FanCurveTable& gpu,std::shared_ptr<DeviceOperation> operation) {
    static_cast<void>(buildAutonomousFanTable(points(cpu)));
    static_cast<void>(buildAutonomousFanTable(points(gpu)));
    FanCurveProfiles profiles; profiles.linked = true; profiles.selected.fill(FanCurvePreset::Manual);
    profiles.ecManual = true; profiles.ecManualCurve = cpu;
    Impl::Request request{Impl::Kind::EcManual}; request.savedProfiles = profiles;request.operation=std::move(operation);
    impl_->request(std::move(request));
}
std::future<void> FanController::ecManualAndWait(const FanCurveTable& curve,std::shared_ptr<DeviceOperation> operation){
    static_cast<void>(buildAutonomousFanTable(points(curve)));
    FanCurveProfiles profiles;profiles.linked=true;profiles.selected.fill(FanCurvePreset::Manual);profiles.ecManual=true;profiles.ecManualCurve=curve;
    auto completion=std::make_shared<std::promise<void>>();auto future=completion->get_future();
    Impl::Request request{Impl::Kind::EcManual};request.savedProfiles=profiles;request.operation=std::move(operation);request.native=completion;
    impl_->request(std::move(request));return future;
}
void FanController::native(std::shared_ptr<DeviceOperation> operation,bool resetApplicationConsumers) { Impl::Request request{Impl::Kind::Native};request.operation=std::move(operation);request.resetApplicationConsumers=resetApplicationConsumers;impl_->request(std::move(request)); }
void FanController::maximum(std::shared_ptr<DeviceOperation> operation) { Impl::Request request{Impl::Kind::Maximum};request.operation=std::move(operation);impl_->request(std::move(request)); }
void FanController::link(bool linked) { impl_->request({Impl::Kind::Link,0,{},linked}); }
void FanController::restoreApplicationPresence() { impl_->presencePending.store(true);impl_->condition.notify_all(); }
void FanController::suspendPower() { impl_->suspendPower(); }
void FanController::resumePower(bool sourceChanged) { impl_->resumePower(sourceChanged); }
void FanController::sessionDisplayChanged(unsigned state) { impl_->sessionDisplayChanged(state); }
void FanController::setConfigurationRestoreNotifier(std::function<void()> notifier) {
    impl_->configurationRestoreNotifier.store(notifier ? std::make_shared<const std::function<void()>>(std::move(notifier)) : nullptr);
}
void FanController::keyboardActivity() {
    const auto now = GetTickCount64(); const auto previous = impl_->lastKeyboardAt.exchange(now);
    if (previous && now-previous < 1000) return;
    impl_->keyboardDue.store(now+75); impl_->condition.notify_all();
}
std::future<PerformanceChange> FanController::performance(int preset, std::shared_ptr<DeviceOperation> operation) {
    auto result = std::make_shared<std::promise<PerformanceChange>>();
    auto future = result->get_future();
    Impl::Request request{Impl::Kind::Performance, preset}; request.operation = std::move(operation); request.performance = result;
    impl_->request(std::move(request));
    return future;
}
std::future<PerformanceChange> FanController::customPower(const std::array<int,3>& values,std::shared_ptr<DeviceOperation> operation){
    auto result=std::make_shared<std::promise<PerformanceChange>>();auto future=result->get_future();
    Impl::Request request{Impl::Kind::Performance,1};request.operation=std::move(operation);
    request.performance=result;request.customPower=values;impl_->request(std::move(request));return future;
}
std::future<PerformanceChange> FanController::customBase(int base,std::shared_ptr<DeviceOperation> operation){
    if(base!=0&&base!=1)throw std::invalid_argument("Custom Base: invalid selection");
    auto result=std::make_shared<std::promise<PerformanceChange>>();auto future=result->get_future();
    Impl::Request request{Impl::Kind::Performance,1};request.operation=std::move(operation);
    request.performance=result;request.customBase=base;impl_->request(std::move(request));return future;
}
std::future<void> FanController::nativeAndWait(std::shared_ptr<DeviceOperation> operation,bool resetApplicationConsumers) {
    auto result = std::make_shared<std::promise<void>>();
    auto future = result->get_future();
    Impl::Request request{Impl::Kind::Native}; request.operation = std::move(operation); request.native = result;request.resetApplicationConsumers=resetApplicationConsumers;
    impl_->request(std::move(request));
    return future;
}
FanDiagnosticSession FanController::prepareDiagnostic() {
    const auto owner=impl_;
    if(std::this_thread::get_id()==owner->worker.get_id())
        throw std::runtime_error("Diagnostic FAN preparation cannot await its own worker");
    auto result=std::make_shared<std::promise<std::shared_ptr<Impl::DiagnosticPlan>>>();
    auto future=result->get_future();Impl::Request request{Impl::Kind::DiagnosticPrepare};request.diagnosticPrepared=result;
    owner->request(std::move(request));const auto plan=future.get();
    // No strong owner capture in the stored callback: GUI/job lifetime owns the
    // controller. A temporary strong lock exists only on the calling dump thread,
    // so the worker cannot become its own final destructor/join owner.
    std::weak_ptr<Impl> weak=owner;
    return {plan->previousMode,[weak,plan]{
        if(const auto current=weak.lock())return current->restoreDiagnostic(plan);
        return FanDiagnosticRestore{false,false,"Diagnostic FAN restore: controller lifetime ended",false};
    }};
}
FanControlStatus FanController::status() const { return impl_->status(); }
std::future<FanMaximumResult> FanController::measureMaximum(std::stop_token cancellation){
    auto promise=std::make_shared<std::promise<FanMaximumResult>>();auto result=promise->get_future();
    Impl::Request request{Impl::Kind::MaximumProbe};request.probe=promise;request.probeCancellation=cancellation;
    impl_->request(std::move(request));return result;
}
}
