#include "MagicControl/IEcBackend.h"
#include "MagicControl/Errors.h"

namespace mc {

std::unique_ptr<IEcBackend> createLinuxEcSysBackend() {
    throw BackendUnavailable(
        "Linux EC transport is not available in this build",
        "The common register core is ready; implement the board-specific Linux transport "
        "through the kernel API before enabling writes.");
}

std::unique_ptr<IEcBackend> createWindowsAcpiDriverBackend() {
    throw BackendUnavailable("The Windows ACPI backend is unavailable on Linux",
                             "Run the Windows build or use the Linux backend.");
}

std::unique_ptr<IEcBackend> createDefaultBackend() { return createLinuxEcSysBackend(); }

}
