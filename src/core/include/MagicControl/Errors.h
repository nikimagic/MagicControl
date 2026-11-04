// MagicControl - error types shared by every layer.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mc {

/// Base class so callers can catch every MagicControl failure at once.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Observation reads temporarily yield to another EC transaction. This is not
/// an I/O failure, a permission grant, or evidence that the machine is asleep.
class EcObservationBusy : public Error {
public:
    using Error::Error;
};

/// The platform backend could not be opened at all (missing driver, missing
/// kernel module, no permission). Carries a hint on how to fix it.
class BackendUnavailable : public Error {
public:
    BackendUnavailable(std::string message, std::string hint)
        : Error(std::move(message)), hint_(std::move(hint)) {}

    [[nodiscard]] const std::string& hint() const noexcept { return hint_; }

private:
    std::string hint_;
};

/// A single EC transaction failed.
class EcIoError : public Error {
public:
    EcIoError(std::uint16_t address, std::string message, unsigned long nativeCode = 0)
        : Error(std::move(message)), address_(address), nativeCode_(nativeCode) {}

    [[nodiscard]] std::uint16_t address() const noexcept { return address_; }
    /// OS error code (Win32 GetLastError or errno), 0 when not applicable.
    [[nodiscard]] unsigned long nativeCode() const noexcept { return nativeCode_; }

private:
    std::uint16_t address_;
    unsigned long nativeCode_;
};

/// A read did not return two consecutive identical bytes: the EC was busy.
class UnstableRead : public EcIoError {
public:
    UnstableRead(std::uint16_t address, std::string message)
        : EcIoError(address, std::move(message)) {}
};

/// A write did not read back as requested, so the EC ignored or clamped it.
class WriteNotConfirmed : public EcIoError {
public:
    WriteNotConfirmed(std::uint16_t address, std::uint8_t requested, std::uint8_t actual)
        : EcIoError(address, "EC 0x" + toHex(address) + " did not confirm the write: requested 0x" +
                                 toHex(requested) + ", read back 0x" + toHex(actual)),
          requested_(requested),
          actual_(actual) {}

    [[nodiscard]] std::uint8_t requested() const noexcept { return requested_; }
    [[nodiscard]] std::uint8_t actual() const noexcept { return actual_; }

private:
    std::uint8_t requested_;
    std::uint8_t actual_;

    [[nodiscard]] static std::string toHex(std::uint32_t value);
};

/// The board identity check failed, so write access stays disabled.
class BoardNotVerified : public Error {
public:
    using Error::Error;
};

/// The requested register cannot be reached through the active transport.
class AddressNotReachable : public EcIoError {
public:
    AddressNotReachable(std::uint16_t address, std::uint16_t limit, const std::string& transport)
        : EcIoError(address, "EC 0x" + toHex(address) + " is outside the " + transport +
                                 " window (limit 0x" + toHex(limit) + ")"),
          limit_(limit) {}

    [[nodiscard]] std::uint16_t limit() const noexcept { return limit_; }

private:
    std::uint16_t limit_;

    [[nodiscard]] static std::string toHex(std::uint32_t value);
};

/// Formats a value as upper-case hexadecimal without a leading 0x.
[[nodiscard]] std::string toHex(std::uint32_t value);

}  // namespace mc
