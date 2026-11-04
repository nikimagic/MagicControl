#include "MagicControl/Types.h"

namespace mc {

const char* toString(FanChannel channel) noexcept {
    switch (channel) {
        case FanChannel::Cpu:
            return "CPU";
        case FanChannel::Gpu:
            return "GPU";
    }
    return "unknown";
}

const char* toString(EcTransport transport) noexcept {
    switch (transport) {
        case EcTransport::WindowsAcpiDriver:
            return "windows-acpi-driver";
        case EcTransport::LinuxEcSys:
            return "linux-ec-sys";
        case EcTransport::LinuxUniwillWmi:
            return "linux-uniwill-wmi";
        case EcTransport::Unknown:
            break;
    }
    return "unknown";
}

}  // namespace mc
