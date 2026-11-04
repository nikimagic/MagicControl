// MagicControl - hardware inventory model.
//
// The BIOS page shows read-only facts about the machine, grouped into named
// sections. The model is platform independent on purpose: the Windows reader
// fills it from the registry and SetupAPI, a future Linux reader will fill it
// from sysfs and dmidecode, and the interface never learns which was used.
#pragma once

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace mc {

/// The text used whenever a value cannot be determined.
///
/// A missing property stays explicitly unknown rather than being rendered as an
/// empty cell, because "not reported" and "reported as empty" are different
/// facts about the hardware.
inline constexpr const char* kInventoryUnknown = "Не сообщается";

/// One caption/value pair, as shown in a single inventory row.
struct InventoryRow {
    std::string name;
    std::string value;
};

/// A titled group of rows, drawn as one card on the BIOS page.
struct InventorySection {
    std::string title;
    std::vector<InventoryRow> rows;

    /// Appends a row and returns it, so a caller can adjust the value after
    /// adding it.
    InventoryRow& addRow(std::string name, std::string value);
};

/// Everything the BIOS page displays, plus when it was collected.
struct HardwareInventory {
    std::string readAt;  ///< Formatted timestamp, shown above the tables.
    std::vector<InventorySection> sections;

    [[nodiscard]] bool empty() const noexcept { return sections.empty(); }

    /// Appends a section and returns it, so readers can build the inventory
    /// without a separate temporary.
    InventorySection& addSection(std::string title);

    [[nodiscard]] bool hasSection(const std::string& title) const noexcept;

    /// Total number of rows across every section, used by diagnostics.
    [[nodiscard]] std::size_t rowCount() const noexcept;
};

/// A source of hardware facts for the BIOS page.
///
/// The interface is deliberately narrow and free of any operating system notion:
/// there is no registry, no WMI, no sysfs and no dmidecode in it. Each platform
/// fills the same model from whatever it has:
///
///   Windows  SetupAPI, the driver registry and the cached SMBIOS description
///   Linux    sysfs, /proc, /sys/class/drm EDID, and dmidecode when available
///
/// Because the model is identical, the page, the cache file and the interface
/// code are written once and shared.
class IHardwareProvider {
public:
    virtual ~IHardwareProvider() = default;

    IHardwareProvider(const IHardwareProvider&) = delete;
    IHardwareProvider& operator=(const IHardwareProvider&) = delete;

    /// Stable identifier, e.g. "windows-setupapi" or "linux-sysfs".
    [[nodiscard]] virtual const char* id() const noexcept = 0;

    /// Human readable description of where the facts come from. Shown in the
    /// interface so a user can tell which source produced a value.
    [[nodiscard]] virtual const char* description() const noexcept = 0;

    /// Collects the inventory. Must not throw: a section that cannot be read
    /// comes back with a row explaining why, so one failure never blanks the page.
    [[nodiscard]] virtual HardwareInventory collect(std::stop_token cancellation = {}) const = 0;

protected:
    IHardwareProvider() = default;
};

/// The provider for the running platform.
[[nodiscard]] const IHardwareProvider& defaultHardwareProvider();

/// Explicit diagnostic dump only: includes raw HID/ACPI/SCM or Linux LED data.
/// Ordinary INFO refresh uses collect() and never starts this deep inspection.
/// Owner-requested INFO EC identity is separate:17read-only RAM bytes with
/// reviewed identity admission, refreshed only during explicit collection.
[[nodiscard]] HardwareInventory collectDiagnosticInventory(std::stop_token cancellation = {});

/// Directory holding the running executable.
///
/// Declared here because the cache needs it and locating the executable is the
/// only part of this file that differs per platform. Implemented in
/// src/platform/<platform>/.
[[nodiscard]] std::string executableDirectory();

/// Replaces a completed temporary file with the cache on the current platform.
/// Windows uses MoveFileExW(REPLACE_EXISTING); POSIX uses rename(2).
[[nodiscard]] bool replaceFileAtomically(const std::string& source,
                                         const std::string& destination,
                                         std::string& error);

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------

/// Reads and writes the inventory cache.
///
/// The snapshot lives in logs_<actual board> with both persistent identity
/// histories. Opening INFO loads it without hardware collection. Import the
/// legacy executable-adjacent file only if the new file is absent; preserve the
/// original. Refresh atomically replaces this snapshot, never either history.
class InventoryCache {
public:
    /// Full path of the cache file.
    [[nodiscard]] static std::string filePath();

    /// Loads the cached inventory.
    ///
    /// Returns nullopt when the file does not exist, cannot be parsed, or was
    /// written by an unsupported version. Never throws.
    [[nodiscard]] static std::optional<HardwareInventory> load();

    /// Writes the inventory, replacing any previous file.
    ///
    /// The data is written to a temporary file first and then moved into place,
    /// so an interrupted write can never leave a half-written cache behind.
    /// Returns false and fills `error` on failure.
    static bool save(const HardwareInventory& inventory, std::string& error);
};

// ---------------------------------------------------------------------------
// Text format
// ---------------------------------------------------------------------------

/// Serialises an inventory to JSON. Kept here rather than in the cache so the
/// CLI can print the same format.
[[nodiscard]] std::string toJson(const HardwareInventory& inventory);

/// Parses an inventory from JSON. Returns nullopt on malformed input.
[[nodiscard]] std::optional<HardwareInventory> fromJson(const std::string& json);

}  // namespace mc
