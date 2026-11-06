// MagicControl - the only interface the core uses to touch hardware.
//
// An OS may provide several transports. The selected device profile chooses
// a verified transport; a shared driver does not imply a shared register map.
// OS interfaces stay outside the core and protocol implementations.
#pragma once

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "MagicControl/Types.h"
#include "MagicControl/BatteryAging.h"
#include <stdexcept>

namespace mc {
struct BoardDescriptor;

// Observing a byte is not proof that the firmware supports a persistent mode.
// GUI, CLI and lifecycle must use this same native write admission.
struct UsbPowerPriorityStatus {
    std::uint8_t raw{};
    bool writable{};
    std::string reason;
    friend bool operator==(const UsbPowerPriorityStatus&,const UsbPowerPriorityStatus&)=default;
};

/// Capabilities and identity of a backend, used by diagnostics and by the UI to
/// decide which controls to enable.
struct BackendInfo {
    /// Stable identifier, e.g. "windows-acpi-driver" or "linux-ec-sys".
    std::string id;
    /// Human-readable name for logs and the about box.
    std::string displayName;
    EcTransport transport{EcTransport::Unknown};
    /// Highest address the backend accepts. 0xFFFF on Windows, 0xFF on Linux.
    std::uint16_t maxAddress{0};
    /// False when reads work but writes are refused (for example a read-only
    /// ec_sys module loaded without write_support=1).
    bool writable{false};
    /// True when every transaction needs administrator/root rights, so the UI
    /// can explain a permission failure instead of showing a bare error.
    bool requiresElevation{false};
};

/// Raw byte transport to the embedded controller.
///
/// The core owns retries, verification and transaction boundaries. A platform
/// may additionally serialize those boundaries with other MagicControl processes.
class IEcBackend {
public:
    virtual ~IEcBackend() = default;

    IEcBackend(const IEcBackend&) = delete;
    IEcBackend& operator=(const IEcBackend&) = delete;

    [[nodiscard]] virtual const BackendInfo& info() const noexcept = 0;

    /// Reads one byte. Throws EcIoError / AddressNotReachable on failure.
    [[nodiscard]] virtual std::uint8_t read(std::uint16_t address) = 0;

    /// Writes one byte. Throws EcIoError / AddressNotReachable on failure.
    /// Callers must have verified the board identity first.
    virtual void write(std::uint16_t address, std::uint8_t value) = 0;

    /// Recursive on the calling thread. Default hooks suit an in-process or
    /// simulated backend; a platform transport can provide a process-wide gate.
    virtual void lockTransaction() {}
    virtual void unlockTransaction() noexcept {}
    // Optional OS owner admission for lighting writers/rollback. Simulated
    // transports need no OS owner query. This is not an exclusive OEM lease.
    virtual void checkKeyboardWriter() const {}
    // Profile admission is necessary but never sufficient: the actual
    // transport rechecks its device, firmware and I/O before every operation.
    virtual BatteryAgingAccess batteryAgingAccess(const BoardDescriptor&) const noexcept{return BatteryAgingAccess::Unavailable;}
    virtual void checkBatteryAgingWriteAccess(){}
    virtual std::optional<BatteryAgingReading> readBatteryAging(){return {};}
    // Optional firmware FAN selector observation. Exact device backends supply
    // this through the same EC transaction; never a general I2EC reader.
    virtual std::optional<std::array<std::uint8_t,4>> readNativeFanFirmwareSelectors(){return {};}
    virtual std::optional<UsbPowerPriorityStatus> readUsbPriorityStatus(){return {};}
    virtual void setUsbPowerPriority(bool){throw std::runtime_error("USB-C priority transport unavailable");}
    virtual void writeBatteryAgingField(BatteryAgingField,std::uint8_t){throw std::runtime_error("Battery aging transport unavailable");}

    /// Reads a contiguous block. The default implementation issues one read per
    /// byte; a backend may override it with a faster path.
    [[nodiscard]] virtual std::vector<std::uint8_t> readBlock(std::uint16_t address, std::size_t count);

    /// True when the backend can reach the given address.
    [[nodiscard]] bool canReach(std::uint16_t address) const noexcept {
        return address <= info().maxAddress;
    }

protected:
    IEcBackend() = default;
};

/// Creates the backend best suited to the running system.
///
/// Throws BackendUnavailable when no transport can be opened; the exception's
/// hint() explains the fix (install the driver, load ec_sys, run elevated).
[[nodiscard]] std::unique_ptr<IEcBackend> createDefaultBackend();

/// Creates the Windows ACPI-driver backend. Declared on every platform so it
/// can be referenced from tests; throws BackendUnavailable elsewhere.
[[nodiscard]] std::unique_ptr<IEcBackend> createWindowsAcpiDriverBackend();

/// Creates the Linux ec_sys backend.
[[nodiscard]] std::unique_ptr<IEcBackend> createLinuxEcSysBackend();

}  // namespace mc
