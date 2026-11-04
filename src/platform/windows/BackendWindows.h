// MagicControl - Windows backend declarations.
#pragma once

#include <memory>
#include <utility>

#include "MagicControl/IEcBackend.h"
#include "MagicControl/Errors.h"

namespace mc {

// A narrowly classified transient open failure. Identity, permissions and OEM
// ownership failures are deliberately NOT represented by this type.
class WindowsEcNotReady final : public BackendUnavailable {
public:
    WindowsEcNotReady(std::string message,std::string hint,unsigned long code)
        :BackendUnavailable(std::move(message),std::move(hint)),code_(code){}
    unsigned long nativeCode() const noexcept{return code_;}
private:
    unsigned long code_;
};

// OEM DefaultTool read ABI: DWORD address. A padded16-byte reply accommodates
// the SYS's variable-width ACPI integer copy. This factory grants
// no write path and bounds reads to the diagnostic EC RAM window0x000–0xFFF.
[[nodiscard]] std::unique_ptr<IEcBackend> createWindowsAcpiDiagnosticBackend();
// HYDROC reviewed ABI, restricted to native battery policy bits7A6.4–5.
// This does not grant a general EC/FANs/lighting write capability.
[[nodiscard]] std::unique_ptr<IEcBackend> createWindowsAcpiBatteryBackend();
// HYDROC battery transport:7B9/7D0 low7 plus explicit platform4/7 and owned
// RAM ROM-ID FF/04 policy. No7A6, save marker, FAN/APU or other ROM writer.
// Caller checks exact identity, pinned installed SYS, OEM owner and pair rollback.
[[nodiscard]] std::unique_ptr<IEcBackend> createWindowsAcpiBatteryThresholdBackend();
// Explicit factory diagnostics only: 741.0, the four Default masks and mailbox
// F5D/F5E/F5F. Never grants writes to the fan-table payload F00–F5C.
[[nodiscard]] std::unique_ptr<IEcBackend> createWindowsAcpiFactoryBackend();

}  // namespace mc
