// MagicControl - common value types shared by the core and every backend.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mc {

/// Physical fan channels exposed by the embedded controller.
enum class FanChannel : std::uint8_t {
    Cpu = 0,   ///< CPU / primary rotor.
    Gpu = 1,   ///< Second rotor. Shares the housing with the CPU fan on some models.
};

[[nodiscard]] const char* toString(FanChannel channel) noexcept;

/// Raw feedback for one fan: duty cycle as a percentage plus tachometer reading.
struct FanFeedback {
    FanChannel channel{FanChannel::Cpu};
    double dutyPercent{0.0};  ///< EC stores duty as raw/2, so 0..100 in 0.5 steps.
    int rpm{0};
};

/// Result of a single EC read that could not be trusted on the first try.
struct EcSample {
    std::uint16_t address{0};
    std::uint8_t value{0};
};

/// Where an EC transaction is routed. Reported by diagnostics so a user can
/// tell why control is unavailable.
enum class EcTransport {
    Unknown = 0,
    WindowsAcpiDriver,  ///< \\.\ACPIDriver IOCTL interface.
    LinuxEcSys,         ///< /sys/kernel/debug/ec/ec0/io (ec_sys write_support=1).
    LinuxUniwillWmi,    ///< Optional uniwill-laptop hwmon/LED fallback layer.
};

[[nodiscard]] const char* toString(EcTransport transport) noexcept;

/// Operating system the binary was built for.
enum class Platform {
    Windows,
    Linux,
};

[[nodiscard]] constexpr Platform currentPlatform() noexcept {
#if defined(_WIN32)
    return Platform::Windows;
#else
    return Platform::Linux;
#endif
}

[[nodiscard]] constexpr const char* currentPlatformName() noexcept {
    return currentPlatform() == Platform::Windows ? "windows" : "linux";
}

}  // namespace mc
