// MagicControl - the single gate every EC transaction passes through.
//
// Two rules make the fan logic safe:
//   1. Exactly one transaction at a time. Telemetry, settings and curve control
//      must never overlap, otherwise the EC returns torn values.
//   2. Nothing is written before the board identity has been verified once at
//      startup. A wrong-board write can change firmware settings we do not own.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "MagicControl/Errors.h"
#include "MagicControl/IEcBackend.h"
#include "MagicControl/Types.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/BoardDescriptor.h"

namespace mc {

struct FanTelemetry;

/// The three identity bytes that must match before any write is allowed.
struct BoardIdentityByte {
    std::uint16_t address{0};
    std::string name;
    std::uint8_t expected{0};
    std::uint8_t actual{0};
    std::uint8_t mask{0xFF};

    [[nodiscard]] bool matches() const noexcept { return (actual & mask) == (expected & mask); }
};

/// Outcome of the one-time startup verification.
struct BoardCheck {
    bool verified{false};
    std::string error;
    std::vector<BoardIdentityByte> bytes;
    BoardDescriptor board;
    double elapsedMilliseconds{0.0};
};

/// Optional diagnostic accounting for one existing byte-wise block read.
/// Durations use steady-clock microseconds. Lock waits and attempted I/O are
/// retained on exceptions; bytesRead counts only completed backend reads.
/// maxByteAddress is meaningful after a byte attempt (including a failed one);
/// an empty/pre-I/O-failed call keeps maxByteUs/maxByteAddress at zero.
struct EcBlockReadTiming {
    std::uint64_t processWaitUs{}, transportWaitUs{}, ioUs{}, maxByteUs{};
    std::uint16_t maxByteAddress{};
    std::size_t bytesRead{};
    bool detailed{};
};

/// Serialized, verified access to the embedded controller.
///
/// Not copyable. Construct one instance per process and share it.
class EcAccess {
public:
    /// Wraps an already-open backend. Throws std::invalid_argument on nullptr.
    explicit EcAccess(std::unique_ptr<IEcBackend> backend);
    ~EcAccess();

    EcAccess(const EcAccess&) = delete;
    EcAccess& operator=(const EcAccess&) = delete;

    [[nodiscard]] const BackendInfo& info() const noexcept;

    // Uses the cached verified profile. Catalog-only devices return false.
    [[nodiscard]] bool supports(DeviceFeature feature) const noexcept;
    void ensureCapability(DeviceFeature feature, std::string_view protocolId) const;
    // Delegates OS owner checks to the transport, keeping core OS-independent.
    void ensureKeyboardWriteAccess() const;

    // --- Raw access --------------------------------------------------------

    /// Reads one byte, serialized against every other transaction.
    [[nodiscard]] std::uint8_t read(std::uint16_t address);

    /// Reads a byte twice until two consecutive reads agree, so a caller never
    /// acts on a half-updated register.
    [[nodiscard]] std::uint8_t readStable(std::uint16_t address);
    [[nodiscard]] BatteryAgingAccess batteryAgingAccess() const noexcept;
    void ensureBatteryAgingWriteAccess();
    std::optional<BatteryAgingReading> readBatteryAging();
    std::optional<std::array<std::uint8_t,4>> readNativeFanFirmwareSelectors();
    void writeBatteryAgingField(BatteryAgingField field,std::uint8_t value);
    std::optional<UsbPowerPriorityStatus> readUsbPriorityStatus();
    void setUsbPowerPriority(bool performance);

    /// Writes one byte and verifies the read-back. Retries three times before
    /// throwing WriteNotConfirmed. Gated by the board check.
    void writeVerified(std::uint16_t address, std::uint8_t value);

    /// Sends a self-clearing firmware command. The caller verifies the resulting
    /// state, because a command/strobe byte is not a persistent setting.
    void updateBitsCommand(std::uint16_t address, std::uint8_t mask, std::uint8_t bits);

    /// Reads, masks, writes and verifies a bit field. `trigger` bits are ORed
    /// into the value but excluded from verification, because the EC clears
    /// strobe bits itself.
    [[nodiscard]] std::uint8_t updateBitsVerified(std::uint16_t address,
                                                  std::uint8_t mask,
                                                  std::uint8_t bits,
                                                  std::uint8_t trigger = 0);

    /// Reads a contiguous block through the backend, preserving byte-wise I/O.
    /// Optional caller-owned timing is reset on entry and survives exceptions.
    [[nodiscard]] std::vector<std::uint8_t> readBlock(std::uint16_t address, std::size_t count,
                                                    EcBlockReadTiming* timing = nullptr);

    /// Reads one byte as a signed temperature in degrees Celsius.
    [[nodiscard]] int readTemperature(std::uint16_t address);

    // --- Fan telemetry -----------------------------------------------------

    /// Reads duty cycle and RPM for one channel.
    ///
    /// The EC exposes RPM as a 16-bit value split across two registers, and the
    /// high byte can roll over mid-read. Three consistent samples are required.
    /// A single zero (or a single non-zero) sample cannot establish a rotor stop
    /// or start, so at least two agreeing samples are demanded for those cases.
    [[nodiscard]] FanFeedback readFanFeedback(FanChannel channel, bool observation = false);

    // --- Board identity ----------------------------------------------------

    /// Performs the one-time verification and caches the result. Safe to call
    /// repeatedly; only the first call touches hardware. Never throws: the
    /// outcome is reported in the returned structure.
    const BoardCheck& verifyBoard();

    /// True after verification unless a later identity change revoked writes.
    [[nodiscard]] bool boardVerified() const noexcept;
    /// Revoke writes for this process if identity changes after a power transition.
    void invalidateBoardVerification() noexcept;

    /// Throws BoardNotVerified when writes are currently not allowed. Every
    /// write path calls this, so a failed startup check disables writes.
    void ensureWritable() const;

    /// Counters used by diagnostics to prove that no transaction was lost.
    [[nodiscard]] std::uint64_t readCount() const noexcept;
    [[nodiscard]] std::uint64_t writeCount() const noexcept;

    // --- Observation control ----------------------------------------------

    /// Temporarily suppresses observation reads (telemetry polling) so a
    /// multi-step curve update is not interrupted. Nestable.
    void pauseObservations() noexcept;
    void resumeObservations() noexcept;
    [[nodiscard]] bool observationsPaused() const noexcept;

    /// Holds the gate across a complete command, including its verification.
    /// Other commands wait; telemetry can skip this interval without blocking UI.
    class Transaction {
    public:
        explicit Transaction(EcAccess& access);
        ~Transaction();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        Transaction(Transaction&& other) noexcept;
        Transaction& operator=(Transaction&&) = delete;
    private:
        EcAccess* access_;
        std::unique_lock<std::recursive_mutex> lock_;
    };

    /// Small RAII helper around pauseObservations/resumeObservations.
    class ObservationPause {
    public:
        explicit ObservationPause(EcAccess& access) noexcept : access_(&access) {
            access_->pauseObservations();
        }
        ~ObservationPause() { if (access_) access_->resumeObservations(); }

        ObservationPause(const ObservationPause&) = delete;
        ObservationPause& operator=(const ObservationPause&) = delete;
        ObservationPause(ObservationPause&& other) noexcept : access_(other.access_) {
            other.access_ = nullptr;
        }

    private:
        EcAccess* access_;
    };

private:
    friend FanTelemetry readFanTelemetry(EcAccess& access);
    [[nodiscard]] std::optional<FanFeedback> cachedFanTelemetry(FanChannel channel) const;
    void cacheFanTelemetry(FanChannel channel, const FanFeedback& feedback);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mc
