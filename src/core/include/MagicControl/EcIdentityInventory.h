#pragma once
#include "HardwareInventory.h"
#include <array>
#include <optional>
#include <span>
#include <stop_token>
namespace mc {
// EC RAM, not a UEFI variable or a firmware-family ID. 770..77D are14ROM-ID
// bytes;77E..77F are service bytes. Preserve every successfully read00/FF byte.
struct EcIdentityInventory {
    std::optional<std::uint8_t> project;
    std::optional<std::uint8_t> platform;
    std::array<std::optional<std::uint8_t>,16> rom;
    std::string scope,readAt,attemptAt,source,error;
    bool retained{};
    bool complete() const noexcept;
};
inline constexpr const char* kEcIdentityInventoryMetadata="EC RAM snapshot metadata";
inline constexpr const char* kEcProjectCaption="Project ID EC [740]";
inline constexpr const char* kEcPlatformCaption="Платформа EC [7C3]";
inline constexpr const char* kEcRomCaption="ROM ID EC [770–77F]";
std::string ecIdentityTimestamp();
// Raw SMBIOS structure table only; UUID used solely to scope the local cache.
std::string ecIdentitySmbiosScope(std::span<const std::uint8_t> table);
EcIdentityInventory readPlatformInventoryEcIdentity(std::stop_token);
// Called only by explicit INFO/diagnostic collection. Shared persistence and
// projection use existing InventoryCache, never a timer or a new controller.
void appendInventoryEcIdentity(HardwareInventory&,std::stop_token);
// Presentation-only upgrade for old caches: unknown rows, no collection/IO.
void ensureInventoryEcIdentityRows(InventorySection&);
}
