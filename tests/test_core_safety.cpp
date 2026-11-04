#include "TestSupport.h"
#include "TestBoardFixture.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/Errors.h"
#include "MagicControl/FanCurve.h"
#include "MagicControl/FanTelemetry.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/Logging.h"
#include "MagicControl/Performance.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/ResourceProvider.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace mc::tests {
namespace {
struct SimulatedEc {
    std::array<std::uint8_t, 65536> memory{};
    std::recursive_mutex transport;
    std::atomic<int> lockCount{}, unlockCount{};
    int failAddress{-1};
    int failReadAddress{-1},acceptedReads{};
    bool failLockOnce{};
    std::vector<std::uint16_t> failSequence;
    SimulatedEc() {
        memory[ec::kBoardEcVersion] = ec::kBoardEcVersionExpected;
        memory[ec::kBoardSystemId] = ec::kBoardSystemIdExpected;
        memory[ec::kBoardProjectId] = ec::kBoardProjectIdExpected;
        memory[ec::kRamTableCapability] = ec::kRamTableSupportedBit;
    }
};
class FakeBackend final : public IEcBackend {
public:
    explicit FakeBackend(std::shared_ptr<SimulatedEc> state, bool writable = true,std::string id="windows-acpi-driver") : state_(std::move(state)) {
        // Match the Wujie profile's required transport ID, while every
        // operation continues to read/write only the in-memory fake below.
        info_.id = std::move(id);info_.maxAddress = 0xffff; info_.writable = writable;
    }
    const BackendInfo& info() const noexcept override { return info_; }
    std::uint8_t read(std::uint16_t address) override {
        if(address==ec::kAcceptedStapm||address==ec::kAcceptedSlow||address==ec::kAcceptedFast)++state_->acceptedReads;
        if(address==state_->failReadAddress)throw EcIoError(address,"Injected read failure");
        return state_->memory[address];
    }
    void write(std::uint16_t address, std::uint8_t value) override {
        if (!state_->failSequence.empty() && state_->failSequence.front() == address) {
            state_->failSequence.erase(state_->failSequence.begin());
            throw EcIoError(address, "Injected rollback failure");
        }
        if (state_->failAddress == address) {
            state_->failAddress = -1;
            throw EcIoError(address, "Injected transport failure");
        }
        state_->memory[address] = value;
    }
    void lockTransaction() override {
        if (std::exchange(state_->failLockOnce, false)) throw std::runtime_error("Injected gate failure");
        state_->transport.lock(); ++state_->lockCount;
    }
    void unlockTransaction() noexcept override { ++state_->unlockCount; state_->transport.unlock(); }
private:
    BackendInfo info_;
    std::shared_ptr<SimulatedEc> state_;
};

void testCatalogDoesNotGrantAdmission(){
    struct RestoreBoard {~RestoreBoard(){resetTestBoard();}} restore;
    const auto known=readBoardDescriptor();
    MonitoringResourceProvider provider(known,[]{return MonitoringSnapshot{};});
    const auto catalog=provider.observe();
    CHECK(catalog.machine.catalogRecognized&&!catalog.machine.declaredFeatures.empty());
    // An advertised Wujie profile is never permission on another actual board.
    setTestBoard(BoardDescriptor{"other manufacturer","other board",{}, {}});
    auto memory=std::make_shared<SimulatedEc>();
    EcAccess unknown(std::make_unique<FakeBackend>(memory));
    CHECK(!unknown.verifyBoard().verified);
    bool blocked=false;try{unknown.writeVerified(ec::kKeyboardRed,1);}catch(const BoardNotVerified&){blocked=true;}
    CHECK(blocked&&unknown.writeCount()==0&&unknown.readCount()==0);
    // A correct board with a foreign transport is rejected before EC probes.
    resetTestBoard();
    EcAccess wrongTransport(std::make_unique<FakeBackend>(memory,true,"unreviewed-backend"));
    CHECK(!wrongTransport.verifyBoard().verified);
    blocked=false;try{wrongTransport.writeVerified(ec::kKeyboardRed,1);}catch(const BoardNotVerified&){blocked=true;}
    CHECK(blocked&&wrongTransport.writeCount()==0&&wrongTransport.readCount()==0);
    // Correct board and transport still require a matching EC identity.
    memory->memory[ec::kBoardProjectId]=0;
    EcAccess wrongIdentity(std::make_unique<FakeBackend>(memory));
    CHECK(!wrongIdentity.verifyBoard().verified&&wrongIdentity.readCount()>0);
    blocked=false;try{wrongIdentity.writeVerified(ec::kKeyboardRed,1);}catch(const BoardNotVerified&){blocked=true;}
    CHECK(blocked&&wrongIdentity.writeCount()==0);
}

void testMovedGuardsAndSerialization() {
    auto state = std::make_shared<SimulatedEc>();
    EcAccess access(std::make_unique<FakeBackend>(state));
    CHECK(access.verifyBoard().verified);
    {
        EcAccess::ObservationPause first(access);
        auto second = std::move(first);
        CHECK(access.observationsPaused());
    }
    CHECK(!access.observationsPaused());
    std::promise<void> entered, finished;
    auto enteredFuture = entered.get_future();
    auto finishedFuture = finished.get_future();
    std::thread other;
    {
        EcAccess::Transaction first(access);
        auto second = std::move(first);
        CHECK(access.observationsPaused());
        other = std::thread([&] {
            entered.set_value();
            access.writeVerified(ec::kKeyboardRed, 2);
            finished.set_value();
        });
        enteredFuture.wait();
        CHECK(finishedFuture.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
        access.writeVerified(ec::kKeyboardRed, 1);
        CHECK(state->memory[ec::kKeyboardRed] == 1);
    }
    other.join();
    CHECK(state->memory[ec::kKeyboardRed] == 2);
    CHECK(!access.observationsPaused());
    CHECK(state->lockCount == state->unlockCount);
    state->failLockOnce = true;
    bool failed = false;
    try { EcAccess::Transaction transaction(access); } catch (const std::exception&) { failed = true; }
    CHECK(failed);
    CHECK(!access.observationsPaused());
    access.writeVerified(ec::kKeyboardRed, 3);
    CHECK(state->memory[ec::kKeyboardRed] == 3);
}

void testCurveFailureRollsBack() {
    auto state = std::make_shared<SimulatedEc>();
    state->memory[ec::kCustomMode] = 0x08;
    state->memory[ec::kFanControlRespective] = 0x03;
    state->memory[ec::kCpuTableBase] = 35;
    state->memory[ec::kCpuTableBase + ec::kTableDutyOffset] = 40;
    const auto original = state->memory;
    EcAccess access(std::make_unique<FakeBackend>(state));
    CHECK(access.verifyBoard().verified);
    state->failAddress = ec::kCpuTableBase + 2;
    bool failed = false;
    try { applyNativeFanCurve(access, {{30, 7}, {70, 100}}, {}, false, FanCurveTarget::Cpu); }
    catch (const std::exception&) { failed = true; }
    CHECK(failed);
    CHECK(state->memory == original);
    CHECK(!access.observationsPaused());
    CHECK(state->lockCount == state->unlockCount);
}

void testApplyPreservesOtherChannelAndReadsActualState() {
    auto state = std::make_shared<SimulatedEc>();
    state->memory[ec::kGpuTableBase] = 35;
    state->memory[ec::kGpuTableBase + ec::kTableDutyOffset] = 40;
    state->memory[ec::kCpuTableBase + ec::kTableAuxOffset] = 91;
    EcAccess access(std::make_unique<FakeBackend>(state));
    CHECK(access.verifyBoard().verified);
    const std::vector<FanCurvePoint> cpu{{30, 7}, {70, 100}}, gpu{{35, 20}};
    applyNativeFanCurve(access, cpu, {}, false, FanCurveTarget::Cpu);
    const auto actual = readNativeFanState(access);
    CHECK(actual.cpu == cpu);
    CHECK(actual.gpu == gpu);
    CHECK(actual.active);
    CHECK(!actual.shared);
    CHECK(state->memory[ec::kCpuTableBase + ec::kTableAuxOffset] == 91);
    restoreNativeFanMode(access);
    const auto native = readNativeFanState(access);
    CHECK(!native.active);
    CHECK(native.cpu == cpu);
    CHECK(native.gpu == gpu);
    CHECK((state->memory[ec::kCustomMode] & ec::kCustomModeBit) == 0);
    CHECK(!access.observationsPaused());
    state->memory[ec::kCpuTableBase] = 35;
    state->memory[ec::kCpuTableBase + ec::kTableDutyOffset] = 40;
    state->memory[ec::kCustomMode] |= ec::kCustomModeBit;
    state->memory[ec::kRamTableCommit] |= ec::kRamTableCommitBit;
    const auto beforeRestore = state->memory;
    // Default touches only the control masks. Fail one of those writes and
    // verify rollback rather than expecting writes to the retained RAM table.
    state->failAddress = ec::kCustomMode;
    bool failed = false;
    try { restoreNativeFanMode(access); } catch (const std::exception&) { failed = true; }
    CHECK(failed);
    CHECK(state->memory == beforeRestore);
}

void testRollbackFailureDisarmsPartialTable() {
    auto state = std::make_shared<SimulatedEc>();
    EcAccess access(std::make_unique<FakeBackend>(state));
    CHECK(access.verifyBoard().verified);
    state->failSequence = {static_cast<std::uint16_t>(ec::kCpuTableBase + 2), ec::kCpuTableBase};
    bool failed = false;
    try { applyNativeFanCurve(access, {{30, 7}, {70, 100}}, {}, false, FanCurveTarget::Cpu); }
    catch (const std::exception&) { failed = true; }
    CHECK(failed);
    CHECK((state->memory[ec::kRamTableCommit] & ec::kRamTableCommitBit) == 0);
    CHECK((state->memory[ec::kCustomMode] & ec::kCustomModeBit) == 0);
    CHECK(!access.observationsPaused());
}

void testValidationAndReadOnlyGuard() {
    auto state = std::make_shared<SimulatedEc>();
    EcAccess access(std::make_unique<FakeBackend>(state));
    CHECK(access.verifyBoard().verified);
    std::string validError;
    CHECK(validateFanCurve({{20,5},{85,50},{100,100}},validError));
    bool failed = false;
    try { applyNativeFanCurve(access, {{101, 50}}, {}, false, FanCurveTarget::Cpu); }
    catch (const std::exception&) { failed = true; }
    CHECK(failed);
    CHECK(access.writeCount() == 0);
    EcAccess readOnly(std::make_unique<FakeBackend>(state, false));
    CHECK(readOnly.verifyBoard().verified);
    failed = false;
    try { readOnly.writeVerified(ec::kKeyboardRed, 25); } catch (const std::exception&) { failed = true; }
    CHECK(failed);
    CHECK(readOnly.writeCount() == 0);
    access.updateBitsCommand(ec::kControl727, 0x18, 0x18);
    CHECK(state->memory[ec::kControl727] == 0x18);
}

void testTelemetrySampleNeverWrites() {
    auto state = std::make_shared<SimulatedEc>();
    state->memory[ec::kCpuRpmHigh] = 7; state->memory[ec::kCpuRpmLow] = 208;
    state->memory[ec::kGpuRpmHigh] = 3; state->memory[ec::kGpuRpmLow] = 232;
    state->memory[ec::kCpuPwm] = 80; state->memory[ec::kGpuPwm] = 60;
    state->memory[ec::kCpuTemperature] = 45;
    EcAccess access(std::make_unique<FakeBackend>(state));
    {
        EcAccess::ObservationPause pause(access);
        const auto missing = readFanTelemetry(access);
        CHECK(!missing.cpu.rpmAvailable && !missing.cpu.pwmAvailable);
        CHECK(!missing.gpu.rpmAvailable && !missing.gpu.pwmAvailable);
    }
    const auto sample = readFanTelemetry(access);
    CHECK(sample.cpu.rpmAvailable && sample.gpu.rpmAvailable);
    CHECK(sample.cpu.rpm == 2000);
    CHECK(sample.gpu.rpm == 1000);
    CHECK(sample.cpu.dutyPercent == 40.0);
    CHECK(sample.gpu.dutyPercent == 30.0);
    CHECK(sample.ecTemperatureCelsius == 45 && !sample.cpu.temperatureAvailable && !sample.gpu.temperatureAvailable);
    {
        EcAccess::ObservationPause pause(access);
        const auto retained = readFanTelemetry(access);
        CHECK(retained.cpu.rpmAvailable && retained.cpu.pwmAvailable);
        CHECK(retained.gpu.rpmAvailable && retained.gpu.pwmAvailable);
        CHECK(retained.cpu.rpm == 2000 && retained.cpu.dutyPercent == 40.0);
        CHECK(retained.gpu.rpm == 1000 && retained.gpu.dutyPercent == 30.0);
        bool freshReadFailed = false;
        try { static_cast<void>(access.readFanFeedback(FanChannel::Cpu, true)); }
        catch (const std::exception&) { freshReadFailed = true; }
        CHECK(freshReadFailed); // Control verification must not consume cached feedback.
    }
    for (const auto address : {ec::kCpuRpmHigh, ec::kCpuRpmLow, ec::kGpuRpmHigh, ec::kGpuRpmLow,
                               ec::kCpuPwm, ec::kGpuPwm}) state->memory[address] = 0;
    const auto stopped = readFanTelemetry(access);
    CHECK(stopped.cpu.rpmAvailable && stopped.cpu.pwmAvailable && stopped.cpu.rpm == 0 && stopped.cpu.dutyPercent == 0);
    CHECK(stopped.gpu.rpmAvailable && stopped.gpu.pwmAvailable && stopped.gpu.rpm == 0 && stopped.gpu.dutyPercent == 0);
    {
        EcAccess::ObservationPause pause(access);
        const auto retained = readFanTelemetry(access);
        CHECK(retained.cpu.pwmAvailable && retained.cpu.rpmAvailable && retained.cpu.dutyPercent == 0 && retained.cpu.rpm == 0);
        CHECK(retained.gpu.pwmAvailable && retained.gpu.rpmAvailable && retained.gpu.dutyPercent == 0 && retained.gpu.rpm == 0);
    }
    CHECK(access.writeCount() == 0);
}

void testBatteryReadingsSurviveMissedPoll() {
    BatteryReading previous;
    previous.present = true; previous.percent = 66; previous.watts = -14;
    previous.celsius = 31; previous.volts = 15.4; previous.milliamps = 900; previous.cycles = 21;
    previous.flow = BatteryFlow::Discharging; previous.source = "EC";
    stampBatteryReading(previous);
    BatteryReading current;
    current.percent = 65; current.watts = -12; current.source = "Windows";
    retainBatteryReadings(current, previous);
    CHECK(current.percent == 65 && current.watts == -12);
    CHECK(current.celsius == 31 && current.volts == 15.4 && current.milliamps == 900 && current.cycles == 21u);
    CHECK(current.present == true && current.flow == BatteryFlow::Unknown && current.source == "Windows");
    CHECK(current.dynamicStale);
    previous = current;
    current.percent = 0; current.watts = 0; current.celsius = 0; current.volts = 16;
    current.milliamps = 0; current.cycles = 0; current.flow = BatteryFlow::Idle;
    retainBatteryReadings(current, previous);
    CHECK(current.percent == 0 && current.watts == 0 && current.milliamps == 0 && current.cycles == 0u);
    CHECK(current.celsius == 0 && current.volts == 16 && current.flow == BatteryFlow::Idle);
    current.present = false;
    retainBatteryReadings(current, previous);
    CHECK(!current.percent && !current.watts && !current.celsius && !current.volts && !current.milliamps && !current.cycles);
    BatteryReading reconnected;
    reconnected.present = true;
    retainBatteryReadings(reconnected, current);
    CHECK(!reconnected.percent && !reconnected.volts && !reconnected.cycles);
}

void testPerformancePreservesFansAndRollsBack() {
    auto memory = std::make_shared<SimulatedEc>();
    EcAccess access(std::make_unique<FakeBackend>(memory));
    CHECK(access.verifyBoard().verified);
    memory->memory[ec::kPerformanceMode] = ec::kPerformanceOffice;
    memory->memory[ec::kCustomMode] = 0x88;
    memory->memory[ec::kControl727] = 0x49;
    memory->memory[ec::kRamTableCommit] = 4;
    memory->memory[ec::kFanControlRespective] = 0x88;
    for (int i=0;i<96;++i) memory->memory[ec::kCpuTableBase+i] = static_cast<std::uint8_t>(i+1);
    for (const auto base : {0x734,0x730,0x7A7}) {
        memory->memory[base] = base == 0x734 ? 45 : base == 0x730 ? 54 : 100;
        memory->memory[base+1] = 65; memory->memory[base+2] = 105;
    }
    memory->memory[ec::kPowerLimitSustained] = 30;
    memory->memory[ec::kPowerLimitShort] = 0;
    memory->memory[ec::kPowerLimitPeak] = 255;
    const auto power = readEcPerformance(access);
    CHECK(power.presets[0].sustained == 45);
    CHECK(power.presets[1].sustained == 54);
    CHECK(power.presets[2].sustained == 100);
    CHECK(power.active.sustained == 30);
    CHECK(power.active.shortBoost == 65);
    CHECK(power.active.peak == 255); // Explicit nonzero override; not a factory limit.
    const auto before = memory->memory;
    const auto changed = selectEcPerformance(access, 1);
    CHECK(changed.before == ec::kPerformanceOffice && changed.after == ec::kPerformanceBalanced);
    bool untouched = true;
    for (std::size_t i=0;i<before.size();++i) if (i!=0x751 && i!=0x783 && i!=0x784 && i!=0x785)
        untouched &= before[i] == memory->memory[i];
    CHECK(untouched);
    CHECK(memory->memory[0x783] == 0 && memory->memory[0x784] == 0 && memory->memory[0x785] == 0);
    const auto writes = access.writeCount();
    static_cast<void>(selectEcPerformance(access, 1));
    CHECK(access.writeCount() == writes);
    for (const auto fail : {0x784,0x751}) {
        memory->memory = before;
        memory->memory[0x783] = 30; memory->memory[0x784] = 50; memory->memory[0x785] = 70;
        const auto original = memory->memory;
        memory->failAddress = fail;
        bool failed = false;
        try { static_cast<void>(selectEcPerformance(access, 2)); } catch (const std::exception&) { failed = true; }
        CHECK(failed);
        CHECK(memory->memory == original);
    }
    DeviceOperationGate gate;
    int notifications = 0;
    gate.setNotifier([&] { ++notifications; });
    auto operation = gate.tryBegin();
    CHECK(operation && gate.busy() && gate.owns(operation));
    CHECK(!gate.tryBegin());
    auto retained = operation;
    operation.reset();
    CHECK(gate.busy());
    retained.reset();
    CHECK(!gate.busy() && notifications == 2);
    auto next = gate.tryBegin();
    CHECK(next != nullptr);
}

void testLogRotationAndDisabledLogging() {
    const auto folder = portableSettingsPath("test-logs-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const bool previous = loggingEnabled();
    std::filesystem::create_directory(folder);
    struct Cleanup {
        std::filesystem::path folder;
        bool previous;
        ~Cleanup() {
            static_cast<void>(initializeLogging(false));
            setLoggingEnabled(previous);
            std::error_code error;
            for (const auto* name : {"MagicControl-1.jsonl", "MagicControl-2.jsonl", "MagicControl-3.jsonl", "preserve.txt"})
                std::filesystem::remove(folder / name, error);
            std::filesystem::remove(folder, error);
        }
    } cleanup{folder, previous};
    const auto now = std::filesystem::file_time_type::clock::now();
    for (int slot = 1; slot <= 3; ++slot) {
        const auto file = folder / ("MagicControl-" + std::to_string(slot) + ".jsonl");
        { std::ofstream output(file); output << "old-session-" << slot; }
        std::filesystem::last_write_time(file, now - std::chrono::hours(slot == 2 ? 3 : slot == 1 ? 2 : 1));
    }
    { std::ofstream output(folder / "preserve.txt"); output << "preserve"; }
    CHECK(initializeLogging(true, folder));
    CHECK(currentLogPath().filename() == "MagicControl-2.jsonl");
    logMessage("test", "quoted \"text\"\nnewline");
    const auto read = [](const std::filesystem::path& file) {
        std::ifstream input(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    const auto written = read(folder / "MagicControl-2.jsonl");
    CHECK(written.find("old-session") == std::string::npos);
    CHECK(written.find("session_start") != std::string::npos);
    CHECK(written.find("\\\"text\\\"\\u000anewline") != std::string::npos);
    CHECK(read(folder / "MagicControl-1.jsonl") == "old-session-1");
    CHECK(read(folder / "MagicControl-3.jsonl") == "old-session-3");
    CHECK(initializeLogging(false, folder / "disabled"));
    logMessage("test", "must not be written");
    CHECK(!std::filesystem::exists(folder / "disabled"));
    CHECK(read(folder / "MagicControl-2.jsonl") == written);
    CHECK(read(folder / "preserve.txt") == "preserve");
}

void testPortableSettingsAtomicLoadAndValidation() {
    const auto folder = portableSettingsPath("test-settings-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    struct Cleanup {
        std::filesystem::path folder;
        ~Cleanup() {
            std::error_code error;
            for (const auto& name : {"app-settings.ini", "fan-curves.csv", "app-settings.ini.tmp", "fan-curves.csv.tmp"})
                std::filesystem::remove(folder / name, error);
            std::filesystem::remove(folder, error);
        }
    } cleanup{folder};
    std::string error;
    ApplicationPreferences expected{}, loaded;expected.allLogs=false;
    CHECK(saveApplicationPreferences(folder / "app-settings.ini", expected, error));
    CHECK(loadApplicationPreferences(folder / "app-settings.ini", loaded, error));
    CHECK(loaded == expected);
    { std::ofstream file(folder / "app-settings.ini"); file << "Version=1\nEmbeddedFont=0\n"; }
    CHECK(loadApplicationPreferences(folder / "app-settings.ini", loaded, error));
    CHECK(loaded.allLogs); // Older configurations retain logging by default.
    CHECK(loaded.embeddedFont); // Legacy opt-out is accepted; bundled font remains mandatory.
    CHECK(saveApplicationPreferences(folder / "app-settings.ini", expected, error));
    CHECK(loadApplicationPreferences(folder / "app-settings.ini", loaded, error));
    for (const auto* invalid : {"AllLogs=2\n", "AllLogs=0\nAllLogs=1\n"}) {
        { std::ofstream file(folder / "app-settings.ini"); file << "Version=1\n" << invalid; }
        CHECK(!loadApplicationPreferences(folder / "app-settings.ini", loaded, error));
        CHECK(loaded == expected);
    }
    { std::ofstream file(folder / "app-settings.ini"); file << "Version=1\nOsdDisabled=9\n"; }
    CHECK(!loadApplicationPreferences(folder / "app-settings.ini", loaded, error));
    CHECK(loaded == expected);
    FanCurveTable cpu{}, gpu{};
    cpu[0] = gpu[0] = {30, 7}; cpu[1] = gpu[1] = {70, 100};
    CHECK(saveFanCurveSettings(folder / "fan-curves.csv", cpu, gpu, error));
    FanCurveTable actualCpu{}, actualGpu{};
    CHECK(loadFanCurveSettings(folder / "fan-curves.csv", actualCpu, actualGpu, error));
    CHECK(actualCpu == cpu);
    CHECK(actualGpu == gpu);
    { std::ofstream file(folder / "fan-curves.csv"); file << "Version,1\nCPU,1,101,50\nGPU,1,70,100\n"; }
    CHECK(!loadFanCurveSettings(folder / "fan-curves.csv", actualCpu, actualGpu, error));
    CHECK(actualCpu == cpu);
    { std::ofstream file(folder / "fan-curves.csv"); file << "Version,1\nCPU,1,30,7,extra\nGPU,1,70,100\n"; }
    CHECK(!loadFanCurveSettings(folder / "fan-curves.csv", actualCpu, actualGpu, error));
    CHECK(actualCpu == cpu);
    cpu[1] = {101, 50};
    CHECK(!saveFanCurveSettings(folder / "fan-curves.csv", cpu, gpu, error));
    CHECK(!error.empty());
    cpu[1] = {70, 100}; cpu[2] = {255, 101};
    CHECK(!saveFanCurveSettings(folder / "fan-curves.csv", cpu, gpu, error));
    bool rejectedPath = false;
    try { static_cast<void>(portableSettingsPath("../outside.ini")); } catch (const std::exception&) { rejectedPath = true; }
    CHECK(rejectedPath);
}
}

int runFanPresetTests() {
    const int before = checkCount(), failures = failureCount();
    const char* temporaryRoot = std::getenv("MAGICCONTROL_TEST_TMP");
    const auto folder = (temporaryRoot ? std::filesystem::path(temporaryRoot) : std::filesystem::temp_directory_path()) /
        ("magiccontrol-fan-presets-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    const auto path = folder / "fan-curves.csv";
    struct Cleanup {
        std::filesystem::path path, folder;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            auto temporary = path; temporary += ".tmp";
            std::filesystem::remove(temporary, ignored);
            std::filesystem::remove(folder, ignored);
        }
    } cleanup{path, folder};
    const auto bytes = [&] {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    const auto write = [&](const std::string& text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc); file << text;
    };
    FanCurveTable cpu{}, gpu{};
    cpu[0] = {30, 7}; cpu[1] = {70, 100};
    gpu[0] = {33, 10}; gpu[1] = {75, 100};
    std::string error;
    CHECK(saveFanCurveSettings(path, cpu, gpu, error));
    FanCurveProfiles profiles;
    CHECK(loadFanCurveProfiles(path, profiles, error));
    CHECK(profiles.current(0) == cpu);
    CHECK(profiles.current(1) == gpu);
    const auto manual = profiles.curves[0];
    profiles.selected[0] = FanCurvePreset::Max;
    profiles.selected[1] = FanCurvePreset::Default;
    CHECK(profiles.current(0)[0] == (FanCurveCell{20, 100}));
    CHECK(std::all_of(profiles.current(0).begin(), profiles.current(0).end(),
        [](const auto& cell) { return cell.duty == 100; }));
    CHECK(profiles.current(1) == FanCurveTable{});
    CHECK(profiles.curves[0] == manual);
    CHECK(saveFanCurveProfiles(path, profiles, error));
    CHECK(error.empty());
    const auto validFile = bytes();
    CHECK(validFile.starts_with("Version,3\r\nMode,Default,Native\r\n"));
    FanCurveProfiles loaded;
    CHECK(loadFanCurveProfiles(path, loaded, error));
    CHECK(loaded == profiles);
    profiles.selected.fill(FanCurvePreset::Manual);
    CHECK(profiles.current(0) == cpu);
    CHECK(profiles.current(1) == gpu);

    // Editing a displayed Max creates Manual; the fixed Max and Default stay intact.
    profiles.selected[0] = FanCurvePreset::Max;
    auto draft = profiles.current(0); draft[0] = {25, 90}; draft[1] = {70, 100};
    profiles.edit(0, draft);
    CHECK(profiles.selected[0] == FanCurvePreset::Manual);
    CHECK(profiles.current(0) == draft);
    CHECK(profiles.curves[2] == FanCurveProfiles{}.curves[2]);
    CHECK(profiles.curves[0][1] == gpu);
    FanCurveProfiles linkedProfiles;
    linkedProfiles.edit(1, gpu);
    linkedProfiles.linked = true;
    CHECK(linkedProfiles.current(0) == linkedProfiles.current(1));
    CHECK(linkedProfiles.curves[0][1] == gpu);
    linkedProfiles.edit(1, cpu);
    CHECK(linkedProfiles.current(0) == cpu);
    CHECK(linkedProfiles.current(1) == cpu);
    CHECK(linkedProfiles.currentPreset(0) == FanCurvePreset::Manual);
    CHECK(linkedProfiles.curves[0][1] == gpu);
    linkedProfiles.linked = false;
    CHECK(linkedProfiles.current(1) == gpu);
    CHECK(linkedProfiles.curves[static_cast<std::size_t>(FanCurvePreset::Min)][0] ==
        FanCurveProfiles{}.curves[static_cast<std::size_t>(FanCurvePreset::Min)][0]);
    CHECK(saveFanCurveProfiles(path, profiles, error));
    CHECK(loadFanCurveProfiles(path, loaded, error));
    CHECK(loaded == profiles);
    profiles.curves[0][0][0].duty = 101;
    const auto beforeInvalidSave = bytes();
    CHECK(!saveFanCurveProfiles(path, profiles, error));
    CHECK(bytes() == beforeInvalidSave);

    // Reject malformed/partial files without changing the in-memory profiles.
    const auto original = loaded;
    write(validFile + "Selected,CPU,Manual\r\n");
    CHECK(!loadFanCurveProfiles(path, loaded, error)); CHECK(loaded == original);
    write(validFile.substr(0, validFile.rfind("Max,GPU,16,")));
    CHECK(!loadFanCurveProfiles(path, loaded, error)); CHECK(loaded == original);
    auto wrong = validFile;
    wrong.replace(wrong.find("Mode,Default,Native"), std::string("Mode,Default,Native").size(), "Mode,Default,255");
    write(wrong);
    CHECK(!loadFanCurveProfiles(path, loaded, error)); CHECK(loaded == original);
    wrong = validFile;
    wrong.replace(wrong.find("Max,CPU,1,20,100"), std::string("Max,CPU,1,20,100").size(), "Max,CPU,1,20,90");
    write(wrong);
    CHECK(!loadFanCurveProfiles(path, loaded, error)); CHECK(loaded == original);
    wrong = validFile;
    wrong.replace(wrong.find("Default,CPU,1,255,100"), std::string("Default,CPU,1,255,100").size(), "Default,CPU,1,30,50");
    write(wrong);
    CHECK(!loadFanCurveProfiles(path, loaded, error)); CHECK(loaded == original);
    CHECK(saveFanCurveProfiles(path, FanCurveProfiles{}, error));
    CHECK(loadFanCurveProfiles(path, loaded, error));
    CHECK(loaded == FanCurveProfiles{});

    // All EC operations below use simulated memory, never a real backend.
    auto simulated = std::make_shared<SimulatedEc>();
    EcAccess access(std::make_unique<FakeBackend>(simulated));
    CHECK(access.verifyBoard().verified);
    const auto beforeDefault = simulated->memory;
    bool rejectedEmpty{};
    try { applyNativeFanCurve(access, {}, {}, false); }
    catch (const std::invalid_argument&) { rejectedEmpty = true; }
    CHECK(rejectedEmpty);
    CHECK(simulated->memory == beforeDefault);
    applyNativeFanCurve(access, {{20, 100}}, {{20, 100}}, false);
    CHECK(simulated->memory[ec::kCpuTableBase + ec::kTableDutyOffset] == 200);
    CHECK(simulated->memory[ec::kGpuTableBase + ec::kTableDutyOffset] == 200);
    CHECK(readNativeFanState(access).active);
    CHECK(!readNativeFanState(access).defaultMode);
    // Software linking must never select firmware common/native mode.
    const std::vector<FanCurvePoint> linkedCurve{{30, 10}, {70, 100}};
    simulated->memory[ec::kFanControlRespective] |= 0x05;
    applyNativeFanCurve(access, linkedCurve, linkedCurve, true);
    auto linkedState = readNativeFanState(access);
    CHECK(linkedState.active);
    CHECK(!linkedState.shared);
    CHECK(!linkedState.defaultMode);
    CHECK(linkedState.cpu == linkedCurve);
    CHECK(linkedState.gpu == linkedCurve);
    CHECK((simulated->memory[ec::kFanControlRespective] & 0x87) == 0x85);
    CHECK((simulated->memory[ec::kRamTableCommit] & ec::kRamTableCommitBit) != 0);
    CHECK((simulated->memory[ec::kControl727] & ec::kControl727FanBit) != 0);
    // Link selection does not establish or revoke the active FAN flags.
    simulated->memory[ec::kFanControlRespective] &= static_cast<std::uint8_t>(~ec::kSplitModeBit);
    CHECK(readNativeFanState(access).active);
    CHECK(readNativeFanState(access).shared);
    applyNativeFanCurve(access, linkedCurve, {{35, 20}, {75, 100}}, false);
    CHECK(readNativeFanState(access).active);
    CHECK(!readNativeFanState(access).shared);
    CHECK(readNativeFanState(access).cpu == linkedCurve);
    CHECK(readNativeFanState(access).gpu != linkedCurve);
    const auto retainedCpuGpuTables=simulated->memory;
    restoreNativeFanMode(access);
    CHECK(readNativeFanState(access).defaultMode);
    CHECK(!readNativeFanState(access).active);
    CHECK(std::equal(simulated->memory.begin() + ec::kCpuTableBase,
        simulated->memory.begin() + ec::kTableEnd,retainedCpuGpuTables.begin() + ec::kCpuTableBase));
    simulated->memory[ec::kControl727] |= ec::kControl727FanBit;
    CHECK(!readNativeFanState(access).defaultMode);
    CHECK(!readNativeFanState(access).active);
    std::printf("Fan presets and native EC mode (simulated): %d checks, %d failure(s)\n",
        checkCount() - before, failureCount() - failures);
    return failureCount() - failures;
}

void testAcceptedWujiePower() {
    auto m=std::make_shared<SimulatedEc>();EcAccess a(std::make_unique<FakeBackend>(m));CHECK(a.verifyBoard().verified);
    m->memory[0x783]=40;m->memory[0x784]=40;m->memory[0x785]=45;
    m->memory[ec::kAcceptedStapm]=25;m->memory[ec::kAcceptedSlow]=25;m->memory[ec::kAcceptedFast]=30;
    auto s=readEcPerformance(a);
    CHECK(s.acceptedValid&&s.accepted.sustained==25&&s.accepted.shortBoost==25&&s.accepted.peak==30);
    CHECK(s.overrides.sustained==40&&s.overrides.shortBoost==40&&s.overrides.peak==45);
    CHECK(a.writeCount()==0); // A clipped result is an observation, never a rewrite.
    m->memory[ec::kAcceptedStapm]=0;s=readEcPerformance(a);CHECK(!s.acceptedValid&&!s.accepted.sustained);
    m->memory[ec::kAcceptedStapm]=255;s=readEcPerformance(a);CHECK(!s.acceptedValid);
    m->memory[ec::kAcceptedStapm]=25;m->failReadAddress=ec::kAcceptedSlow;
    s=readEcPerformance(a);CHECK(!s.acceptedValid&&!s.accepted.shortBoost&&s.overrides.sustained==40);
    m->failReadAddress=-1;m->memory[ec::kBoardProjectId]=27;
    const auto reads=m->acceptedReads;s=readEcPerformance(a);CHECK(!s.acceptedValid&&m->acceptedReads==reads);
    CHECK(a.writeCount()==0);
}
void testCustomPowerBaseAndRollback(){
    for(const auto mode:{0xA0,0x80,0x00,0x10,0xE0,0x50,0xA8}){
        auto m=std::make_shared<SimulatedEc>();EcAccess a(std::make_unique<FakeBackend>(m));CHECK(a.verifyBoard().verified);
        m->memory[0x751]=static_cast<std::uint8_t>(mode);m->memory[0x726]=0x88;m->memory[0x727]=0x49;m->memory[0x7c5]=0x88;m->memory[0x7c6]=4;
        m->memory[0x783]=80;m->memory[0x784]=80;m->memory[0x785]=85;
        for(unsigned i=0;i<96;++i)m->memory[0xf00+i]=static_cast<std::uint8_t>(i+1);
        const auto before=m->memory;const auto result=applyCustomPowerLimits(a,{80,80,85});
        CHECK(result.before==mode&&result.after==(mode&~0xB0));
        CHECK(m->memory[0x783]==80&&m->memory[0x784]==80&&m->memory[0x785]==85);
        bool untouched=true;for(unsigned i=0;i<before.size();++i)if(i!=0x751&&i!=0x783&&i!=0x784&&i!=0x785)untouched&=m->memory[i]==before[i];
        CHECK(untouched);CHECK((m->memory[0x751]&0x40)==(mode&0x40));
        const auto count=a.writeCount();applyCustomPowerLimits(a,{80,80,85});CHECK(a.writeCount()==count);
    }
    for(const auto failure:{0x751,0x783,0x784,0x785}){
        auto m=std::make_shared<SimulatedEc>();EcAccess a(std::make_unique<FakeBackend>(m));CHECK(a.verifyBoard().verified);
        m->memory[0x751]=0xE0;m->memory[0x783]=10;m->memory[0x784]=20;m->memory[0x785]=30;
        const auto before=m->memory;m->failAddress=failure;bool failed=false;
        try{applyCustomPowerLimits(a,{80,80,85});}catch(const std::exception&){failed=true;}
        CHECK(failed);CHECK(m->memory==before);
    }
    for(int invalid:{0,1,2}){
        auto m=std::make_shared<SimulatedEc>();EcAccess a(std::make_unique<FakeBackend>(m));CHECK(a.verifyBoard().verified);
        m->memory[0x751]=invalid==2?0x90:0xA0;if(invalid==1)m->memory[0x727]=0x80;
        const auto before=m->memory;bool refused=false;
        try{applyCustomPowerLimits(a,invalid==0?std::array<int,3>{4,20,30}:std::array<int,3>{10,20,30});}catch(...){refused=true;}
        CHECK(refused&&a.writeCount()==0&&m->memory==before);
    }
}
int runCoreSafetyTests() {
    const int before = checkCount(), failures = failureCount();
    testCatalogDoesNotGrantAdmission();
    testMovedGuardsAndSerialization();
    testCurveFailureRollsBack();
    testApplyPreservesOtherChannelAndReadsActualState();
    testRollbackFailureDisarmsPartialTable();
    testValidationAndReadOnlyGuard();
    testTelemetrySampleNeverWrites();
    testBatteryReadingsSurviveMissedPoll();
    testPortableSettingsAtomicLoadAndValidation();
    testLogRotationAndDisabledLogging();
    testPerformancePreservesFansAndRollsBack();
    testAcceptedWujiePower();
    testCustomPowerBaseAndRollback();
    std::printf("EC transactions and portable settings: %d checks, %d failure(s)\n", checkCount() - before, failureCount() - failures);
    return failureCount() - failures;
}
int runLoggingTests() {
    const int before = checkCount(), failures = failureCount();
    testPortableSettingsAtomicLoadAndValidation();
    testLogRotationAndDisabledLogging();
    std::printf("Logging and portable settings: %d checks, %d failure(s)\n", checkCount() - before, failureCount() - failures);
    return failureCount() - failures;
}
}
