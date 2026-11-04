#pragma once
#include <cstdint>
#include <string_view>
namespace mc::hydroc16g2 {
inline constexpr std::string_view profileId="eluktronics-hydroc16g2";
inline constexpr std::string_view protocolId="uniwill-hydroc16g2-diagnostics";
inline constexpr std::uint16_t lastRam=0x0FFF;
// Pinned OEM11.2.18.347 from ControlCenter5.23.51.56. Read-only onboarding:
// another driver revision must be reviewed, not silently tried with guessed ABI.
inline constexpr std::string_view driverSha256="e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2";
inline constexpr std::uint16_t project=0x740,biosProject=0x74C,ramFanCapability=0x78E,lcFanCapability=0x7C8,keyboardSubtype=0x73C;
}
