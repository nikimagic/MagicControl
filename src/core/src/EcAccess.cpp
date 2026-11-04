#include "MagicControl/EcAccess.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "MagicControl/BoardIdentity.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/ActionLog.h"

namespace mc {
namespace {

/// The EC needs a moment between a write and the read-back that confirms it.
constexpr auto kSettleDelay = std::chrono::milliseconds(2);

/// updateBitsVerified polls longer because apply strobes take time to land.
constexpr auto kBitSettleDelay = std::chrono::milliseconds(20);

constexpr int kStableReadAttempts = 8;
constexpr int kFanFeedbackAttempts = 3;
constexpr int kFanFeedbackSamples = 3;
constexpr int kWriteVerifyAttempts = 3;
constexpr int kBitVerifyAttempts = 8;

void sleepFor(std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); }

struct BackendLock {
    IEcBackend& backend;
    explicit BackendLock(IEcBackend& value) : backend(value) { backend.lockTransaction(); }
    ~BackendLock() { backend.unlockTransaction(); }
};

struct ElapsedMicros {
    std::uint64_t& total;
    const std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    ~ElapsedMicros() noexcept {
        total += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-started).count());
    }
};
struct ByteReadTiming {
    EcBlockReadTiming& timing;
    std::uint16_t address;
    const std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    ~ByteReadTiming() noexcept {
        const auto elapsed=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-started).count());
        timing.ioUs+=elapsed;
        if(elapsed>=timing.maxByteUs){timing.maxByteUs=elapsed;timing.maxByteAddress=address;}
    }
};

}  // namespace

class EcAccess::Impl {
public:
    explicit Impl(std::unique_ptr<IEcBackend> backend) : backend_(std::move(backend)) {}

    std::unique_ptr<IEcBackend> backend_;
    mutable std::recursive_mutex gate_;
    mutable std::mutex fanTelemetryMutex_;
    std::array<std::optional<FanFeedback>, 2> fanTelemetryCache_{};
    std::atomic<std::uint64_t> reads_{0};
    std::atomic<std::uint64_t> writes_{0};
    std::atomic<int> observationPauses_{0};

    std::optional<FanFeedback> cachedFanTelemetry(FanChannel channel) const {
        const auto index = static_cast<std::size_t>(channel);
        if (index >= fanTelemetryCache_.size()) return {};
        std::lock_guard lock(fanTelemetryMutex_);
        return fanTelemetryCache_[index];
    }

    void cacheFanTelemetry(FanChannel channel, const FanFeedback& feedback) {
        const auto index = static_cast<std::size_t>(channel);
        if (index >= fanTelemetryCache_.size()) return;
        std::lock_guard lock(fanTelemetryMutex_);
        fanTelemetryCache_[index] = feedback;
    }

    std::once_flag boardCheckOnce_;
    BoardCheck boardCheck_;
    std::atomic<bool> boardInvalidated_{};

    /// Reads one byte without taking the gate. Callers already hold it.
    std::uint8_t rawRead(std::uint16_t address) {
        reads_.fetch_add(1, std::memory_order_relaxed);
        try{const auto value=backend_->read(address);
            if(audit::current())audit::event("ec_read_response","\"address\":"+std::to_string(address)+",\"value\":"+std::to_string(value)+",\"backend\":"+audit::quote(backend_->info().id));return value;
        }catch(const std::exception& e){if(audit::current())audit::event("ec_read_failed","\"address\":"+std::to_string(address)+",\"error\":"+audit::quote(e.what()));throw;}
    }

    /// Writes one byte without taking the gate. Callers already hold it.
    void rawWrite(std::uint16_t address, std::uint8_t value) {
        if (boardInvalidated_.load()) throw BoardNotVerified("Board identity changed; restart the application to verify the EC before writing.");
        const auto id=deviceProfile(boardCheck_.board).id;
        const bool identity=(address==0x770||address==0x7C3)&&boardCheck_.verified&&backend_->info().transport==EcTransport::WindowsAcpiDriver&&
            (id=="wu-jie-15-xpro"||ramFanControlProfile(id));
        if(identity)recordIdentityByteEvent(address,"write_requested",{},value,{},"pending","ec_access");
        writes_.fetch_add(1, std::memory_order_relaxed);
        audit::event("ec_write_requested","\"address\":"+std::to_string(address)+",\"value\":"+std::to_string(value)+",\"backend\":"+audit::quote(backend_->info().id));
        try{backend_->write(address, value);audit::event("ec_write_returned","\"address\":"+std::to_string(address)+",\"value\":"+std::to_string(value)+",\"readback_confirmed\":false");
            if(identity)recordIdentityByteEvent(address,"write_returned",{},value,{},"unconfirmed","ec_access");}
        catch(const std::exception& e){audit::event("ec_write_failed","\"address\":"+std::to_string(address)+",\"value\":"+std::to_string(value)+",\"error\":"+audit::quote(e.what()));
            if(identity)recordIdentityByteEvent(address,"write_failed",{},value,{},"unconfirmed","ec_access",e.what());throw;}
    }

    /// Assumes the gate is held.
    std::uint8_t readStableLocked(std::uint16_t address) {
        std::uint8_t previous = rawRead(address);
        for (int attempt = 0; attempt < kStableReadAttempts; ++attempt) {
            const std::uint8_t current = rawRead(address);
            if (current == previous) {
                // Only the reviewed Uniwill RAM map; this is an existing read,
                // never an additional probe or a Huawei/preview ROM mapping.
                if((address==0x740||address==0x770||address==0x7C3)&&boardCheck_.verified&&backend_->info().transport==EcTransport::WindowsAcpiDriver){
                    const auto id=deviceProfile(boardCheck_.board).id;
                    if(id=="wu-jie-15-xpro"||ramFanControlProfile(id)){
                        if(address==0x740)observeProjectId(current,"stable_ec_read");
                        else if(address==0x770)observeRomId(current,"stable_ec_read");else observePlatformId(current,"stable_ec_read");
                    }
                }
                return current;
            }
            previous = current;
            sleepFor(kSettleDelay);
        }
        throw UnstableRead(address, "EC 0x" + toHex(address) +
                                        " kept changing across " +
                                        std::to_string(kStableReadAttempts) + " reads");
    }

    void writeVerifiedLocked(std::uint16_t address, std::uint8_t value) {
        for (int attempt = 0; attempt < kWriteVerifyAttempts; ++attempt) {
            rawWrite(address, value);
            sleepFor(kSettleDelay);
            std::uint8_t actual = value;
            bool readable = true;
            try {
                actual = readStableLocked(address);
            } catch (const EcIoError&) {
                readable = false;
            }
            if (readable && actual == value) {
                return;
            }
            if (readable && attempt == kWriteVerifyAttempts - 1) {
                throw WriteNotConfirmed(address, value, actual);
            }
        }
        throw WriteNotConfirmed(address, value, 0);
    }

    std::uint8_t updateBitsVerifiedLocked(std::uint16_t address, std::uint8_t mask,
                                          std::uint8_t bits, std::uint8_t trigger) {
        const std::uint8_t before = readStableLocked(address);
        const std::uint8_t requested =
            static_cast<std::uint8_t>((before & ~mask) | (bits & mask) | trigger);
        rawWrite(address, requested);
        for (int attempt = 0; attempt < kBitVerifyAttempts; ++attempt) {
            sleepFor(kBitSettleDelay);
            const std::uint8_t actual = readStableLocked(address);
            // Apply strobes clear themselves, so only the setting field is checked.
            if ((actual & mask) == (bits & mask)) {
                return actual;
            }
        }
        throw WriteNotConfirmed(address, static_cast<std::uint8_t>(bits & mask), before);
    }
};

EcAccess::EcAccess(std::unique_ptr<IEcBackend> backend) : impl_(nullptr) {
    if (!backend) {
        throw std::invalid_argument("EcAccess requires a backend instance");
    }
    impl_ = std::make_unique<Impl>(std::move(backend));
}

EcAccess::~EcAccess() = default;

EcAccess::Transaction::Transaction(EcAccess& access)
    : access_(&access), lock_(access.impl_->gate_, std::defer_lock) {
    access.pauseObservations();
    try {
        lock_.lock();
        access.impl_->backend_->lockTransaction();
    } catch (...) {
        access.resumeObservations();
        throw;
    }
}

EcAccess::Transaction::Transaction(Transaction&& other) noexcept
    : access_(std::exchange(other.access_, nullptr)), lock_(std::move(other.lock_)) {}

EcAccess::Transaction::~Transaction() {
    if (!access_) return;
    access_->impl_->backend_->unlockTransaction();
    lock_.unlock();
    access_->resumeObservations();
}

const BackendInfo& EcAccess::info() const noexcept { return impl_->backend_->info(); }

bool EcAccess::supports(DeviceFeature feature) const noexcept {
    return boardVerified() && deviceProfile(impl_->boardCheck_.board).capabilities.supports(feature);
}
void EcAccess::ensureCapability(DeviceFeature feature, std::string_view protocolId) const {
    ensureWritable();
    if (!supports(feature) || deviceProfile(impl_->boardCheck_.board).protocolId != protocolId)
        throw BoardNotVerified("This device profile does not support the requested feature/protocol");
}
void EcAccess::ensureKeyboardWriteAccess() const {
    ensureWritable();
    const std::lock_guard<std::recursive_mutex> lock(impl_->gate_);
    impl_->backend_->checkKeyboardWriter();
}

std::uint8_t EcAccess::read(std::uint16_t address) {
    const std::lock_guard<std::recursive_mutex> lock(impl_->gate_);
    const BackendLock transport(*impl_->backend_);
    return impl_->rawRead(address);
}

std::uint8_t EcAccess::readStable(std::uint16_t address) {
    const std::lock_guard<std::recursive_mutex> lock(impl_->gate_);
    const BackendLock transport(*impl_->backend_);
    return impl_->readStableLocked(address);
}

std::optional<BatteryAgingReading> EcAccess::readBatteryAging(){
    const std::lock_guard lock(impl_->gate_);const BackendLock transport(*impl_->backend_);
    if(!boardVerified()||deviceProfile(impl_->boardCheck_.board).id!="wu-jie-15-xpro")return {};
    return impl_->backend_->readBatteryAging();
}
void EcAccess::writeBatteryAgingByte(std::uint16_t address,std::uint8_t value){
    ensureWritable();const std::lock_guard lock(impl_->gate_);const BackendLock transport(*impl_->backend_);
    if(deviceProfile(impl_->boardCheck_.board).id!="wu-jie-15-xpro"||
       (address!=0x7f2&&address!=0x7f3&&address!=0x9c9&&address!=0x9ca&&address!=0x726))
        throw BoardNotVerified("Battery aging address/profile denied");
    audit::event("battery_aging_write","\"address\":"+std::to_string(address)+",\"value\":"+std::to_string(value));
    impl_->writes_.fetch_add(1,std::memory_order_relaxed);
    impl_->backend_->writeBatteryAgingByte(address,value);
}

std::optional<UsbPowerPriorityStatus> EcAccess::readUsbPriorityStatus(){
    const std::lock_guard lock(impl_->gate_);const BackendLock transport(*impl_->backend_);
    if(!boardVerified()||deviceProfile(impl_->boardCheck_.board).id!="wu-jie-15-xpro")return {};
    return impl_->backend_->readUsbPriorityStatus();
}
void EcAccess::setUsbPowerPriority(bool performance){
    ensureCapability(DeviceFeature::BatteryCharge,"uniwill-x5sp4nag");
    const std::lock_guard lock(impl_->gate_);const BackendLock transport(*impl_->backend_);
    audit::Scope trace("usb_priority.write","\"performance\":"+std::string(performance?"true":"false"));
    const auto status=impl_->backend_->readUsbPriorityStatus();
    if(!status||!status->writable)
        throw std::runtime_error(status&&!status->reason.empty()?status->reason:"USB-C priority control unavailable");
    impl_->writes_.fetch_add(1,std::memory_order_relaxed);
    impl_->backend_->setUsbPowerPriority(performance);trace.finish("readback_confirmed");
}

void EcAccess::writeVerified(std::uint16_t address, std::uint8_t value) {
    audit::Scope trace("ec.write_verified","\"address\":"+std::to_string(address)+",\"requested\":"+std::to_string(value));
    ensureWritable();
    const std::lock_guard<std::recursive_mutex> lock(impl_->gate_);
    const BackendLock transport(*impl_->backend_);
    impl_->writeVerifiedLocked(address, value);
    trace.finish("readback_confirmed","\"address\":"+std::to_string(address)+",\"observed\":"+std::to_string(value));
}

void EcAccess::updateBitsCommand(std::uint16_t address, std::uint8_t mask, std::uint8_t bits) {
    audit::Scope trace("ec.masked_command","\"address\":"+std::to_string(address)+",\"mask\":"+std::to_string(mask)+",\"bits\":"+std::to_string(bits));
    ensureWritable();
    const std::lock_guard<std::recursive_mutex> lock(impl_->gate_);
    const BackendLock transport(*impl_->backend_);
    const auto before = impl_->readStableLocked(address);
    impl_->rawWrite(address, static_cast<std::uint8_t>((before & ~mask) | (bits & mask)));
    trace.finish("transport_returned_not_verified","\"before\":"+std::to_string(before));
}

std::uint8_t EcAccess::updateBitsVerified(std::uint16_t address, std::uint8_t mask,
                                          std::uint8_t bits, std::uint8_t trigger) {
    audit::Scope trace("ec.masked_verified","\"address\":"+std::to_string(address)+",\"mask\":"+std::to_string(mask)+",\"bits\":"+std::to_string(bits)+",\"trigger\":"+std::to_string(trigger));
    ensureWritable();
    const std::lock_guard<std::recursive_mutex> lock(impl_->gate_);
    const BackendLock transport(*impl_->backend_);
    const auto actual=impl_->updateBitsVerifiedLocked(address, mask, bits, trigger);
    trace.finish("readback_confirmed","\"observed\":"+std::to_string(actual));return actual;
}

std::vector<std::uint8_t> EcAccess::readBlock(std::uint16_t address, std::size_t count, EcBlockReadTiming* timing) {
    if(!timing){
        // Keep the ordinary path's locking/read-count behavior and avoid adding
        // per-byte clock queries to telemetry that did not request diagnostics.
        const std::lock_guard<std::recursive_mutex> lock(impl_->gate_);
        const BackendLock transport(*impl_->backend_);
        std::vector<std::uint8_t> out;
        out.reserve(count);
        for(std::size_t i=0;i<count;++i)
            out.push_back(impl_->rawRead(static_cast<std::uint16_t>(address+i)));
        return out;
    }
    *timing={};timing->detailed=true;
    std::unique_lock<std::recursive_mutex> lock(impl_->gate_,std::defer_lock);
    {const ElapsedMicros elapsed{timing->processWaitUs};lock.lock();}
    // In-place optional construction retains the same backend RAII lifetime:
    // backend unlock precedes local gate unlock, including exception unwinding.
    std::optional<BackendLock> transport;
    {const ElapsedMicros elapsed{timing->transportWaitUs};transport.emplace(*impl_->backend_);}
    std::vector<std::uint8_t> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t value;
        {const ByteReadTiming elapsed{*timing,static_cast<std::uint16_t>(address+i)};
            value=impl_->rawRead(static_cast<std::uint16_t>(address+i));++timing->bytesRead;}
        out.push_back(value);
    }
    return out;
}

int EcAccess::readTemperature(std::uint16_t address) {
    const std::uint8_t raw = readStable(address);
    return static_cast<int>(static_cast<std::int8_t>(raw));
}

FanFeedback EcAccess::readFanFeedback(FanChannel channel, bool observation) {
    const bool cpu = channel == FanChannel::Cpu;
    const std::uint16_t rpmHighAddr = cpu ? ec::kCpuRpmHigh : ec::kGpuRpmHigh;
    const std::uint16_t rpmLowAddr = cpu ? ec::kCpuRpmLow : ec::kGpuRpmLow;
    const std::uint16_t pwmAddr = cpu ? ec::kCpuPwm : ec::kGpuPwm;

    const std::lock_guard<std::recursive_mutex> lock(impl_->gate_);

    if (observation && observationsPaused())
        throw std::runtime_error("fan observation cancelled: command in progress");
    const BackendLock transport(*impl_->backend_);

    for (int attempt = 0; attempt < kFanFeedbackAttempts; ++attempt) {
        struct Sample {
            std::uint8_t duty;
            int rpm;
        };
        std::vector<Sample> samples;
        samples.reserve(kFanFeedbackSamples);

        for (int sample = 0; sample < kFanFeedbackSamples; ++sample) {
            if (observation && observationsPaused()) {
                throw std::runtime_error("fan observation cancelled: observations are paused");
            }
            const std::uint8_t dutyBefore = impl_->readStableLocked(pwmAddr);
            const std::uint8_t highBefore = impl_->rawRead(rpmHighAddr);
            const std::uint8_t lowByte = impl_->rawRead(rpmLowAddr);
            const std::uint8_t highAfter = impl_->rawRead(rpmHighAddr);
            const std::uint8_t dutyAfter = impl_->readStableLocked(pwmAddr);

            // A sample counts only when the 16-bit tachometer did not roll over
            // between its two halves and the duty byte held still around it.
            // Duty above 200 raw cannot occur (percent = raw / 2) and signals
            // that the EC answered with something other than a duty value.
            if (highBefore == highAfter && dutyBefore == dutyAfter && dutyAfter <= 200) {
                const int rpm = (static_cast<int>(highAfter) << 8) | static_cast<int>(lowByte);
                samples.push_back(Sample{dutyAfter, rpm});
            }
            sleepFor(kSettleDelay);
        }

        if (static_cast<int>(samples.size()) < kFanFeedbackSamples) {
            continue;
        }

        // Pick the duty value shared by most samples: that is the settled state.
        std::uint8_t bestDuty = 0;
        std::size_t bestCount = 0;
        for (const Sample& candidate : samples) {
            const std::size_t count =
                static_cast<std::size_t>(std::count_if(samples.begin(), samples.end(),
                                                       [candidate](const Sample& other) {
                                                           return other.duty == candidate.duty;
                                                       }));
            if (count > bestCount) {
                bestCount = count;
                bestDuty = candidate.duty;
            }
        }
        if (bestCount < 2) {
            continue;  // No settled duty value: the EC is still moving.
        }

        std::vector<int> positiveRpm;
        std::size_t zeroCount = 0;
        for (const Sample& sample : samples) {
            if (sample.duty != bestDuty) {
                continue;
            }
            if (sample.rpm > 0) {
                positiveRpm.push_back(sample.rpm);
            } else {
                ++zeroCount;
            }
        }
        std::sort(positiveRpm.begin(), positiveRpm.end());

        // One zero or one positive reading cannot establish a rotor stop or
        // start. Demand confirmation inside the same burst.
        if (positiveRpm.size() == 1 || (positiveRpm.empty() && zeroCount < 2)) {
            continue;
        }

        FanFeedback feedback;
        feedback.channel = channel;
        feedback.dutyPercent = static_cast<double>(bestDuty) / 2.0;
        feedback.rpm = positiveRpm.empty() ? 0 : positiveRpm[positiveRpm.size() / 2];
        return feedback;
    }

    throw EcIoError(cpu ? ec::kCpuRpmHigh : ec::kGpuRpmHigh,
                    std::string("no consistent PWM/RPM pair for the ") + toString(channel) +
                        " fan after " + std::to_string(kFanFeedbackAttempts) + " bursts");
}

std::optional<FanFeedback> EcAccess::cachedFanTelemetry(FanChannel channel) const {
    return impl_->cachedFanTelemetry(channel);
}

void EcAccess::cacheFanTelemetry(FanChannel channel, const FanFeedback& feedback) {
    impl_->cacheFanTelemetry(channel, feedback);
}

const BoardCheck& EcAccess::verifyBoard() {
    std::call_once(impl_->boardCheckOnce_, [this] { impl_->boardCheck_ = runBoardCheck(*this); });
    return impl_->boardCheck_;
}

bool EcAccess::boardVerified() const noexcept {
    return impl_->boardCheck_.verified && !impl_->boardInvalidated_.load();
}
void EcAccess::invalidateBoardVerification() noexcept { impl_->boardInvalidated_.store(true); }

void EcAccess::ensureWritable() const {
    if (!impl_->backend_->info().writable)
        throw std::runtime_error("The EC backend is read-only");
    if (impl_->boardInvalidated_.load())
        throw BoardNotVerified("Board identity changed after a power transition; EC writes are disabled until restart.");
    if (!boardVerified()) {
        std::string message =
            "board identity is not verified; EC writes are disabled";
        if (!impl_->boardCheck_.error.empty()) {
            message += ": " + impl_->boardCheck_.error;
        }
        throw BoardNotVerified(message);
    }
}

std::uint64_t EcAccess::readCount() const noexcept { return impl_->reads_.load(); }

std::uint64_t EcAccess::writeCount() const noexcept { return impl_->writes_.load(); }

void EcAccess::pauseObservations() noexcept {
    impl_->observationPauses_.fetch_add(1, std::memory_order_relaxed);
}

void EcAccess::resumeObservations() noexcept {
    impl_->observationPauses_.fetch_sub(1, std::memory_order_relaxed);
}

bool EcAccess::observationsPaused() const noexcept {
    return impl_->observationPauses_.load(std::memory_order_relaxed) != 0;
}

}  // namespace mc
