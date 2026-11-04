#pragma once
#include <cstdint>
namespace mc::uniwill {
// Read-only field descriptions shared by board-specific catalogues.
// A documented field never grants permission to write it.
struct BatteryRegisterDefinition {
    std::uint16_t address;
    std::uint8_t width;
    bool bigEndian;
    const char* name;
    bool confirmed;
    bool sampledTelemetry{};
};
struct BatteryBitDefinition {std::uint16_t address;std::uint8_t bit;const char* name;};
}
