#pragma once
#include "MagicControl/IEcBackend.h"
#include <string>
#include <vector>
namespace mc {struct BatteryReading;}
namespace mc::windows {
// Reused by the narrow battery controller; checks exact SMBIOS, PnP/service,
// installed OEM SYS path and pinned SHA256 before opening the device.
void verifyHydrocDriver(std::string& evidence);
// Full RAM/HID integration is reviewed for this exact BIOS revision. Battery
// policy retains its separate pre-existing admission contract.
void verifyHydrocControlIdentity();
// Exact OEM cycle fallback for one fully enumerated physical battery.
// Read-only handle; does not construct a native policy/restore controller.
void readHydrocBatteryTelemetry(BatteryReading&,bool singlePhysicalBattery);
// Construct only for an explicit dump. No startup restoration, setters, polling
// interpretation or reuse of the X5/GM6 EcAccess verification/write permission.
class HydrocDiagnosticSession {
public:
    explicit HydrocDiagnosticSession(std::string& evidence);
    std::vector<std::uint8_t> readBlock(std::uint16_t,std::size_t);
private:std::unique_ptr<IEcBackend> backend_;
};
}
