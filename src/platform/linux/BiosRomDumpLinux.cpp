#include "MagicControl/BiosRomDump.h"
#include "../common/FirmwareEvidence.h"
#include <stdexcept>
namespace mc {
bool biosRomDumpAvailable(){return false;}
std::uint64_t biosRomExpectedBytes(){return 0;}
DiagnosticDumpResult createPlatformBiosRomDump(std::stop_token,std::atomic<std::uint64_t>*){
    DiagnosticDumpResult r;r.message="AMI AFU BIOS reader доступен только в Windows; Linux backend не подключён.";return r;
}
namespace diagnostics {
void collectPlatformBiosRom(const FirmwareEmitter&){throw std::runtime_error("OEM ACPI BIOS reader is Windows-only; Linux SPI transport not confirmed");}
}
}
