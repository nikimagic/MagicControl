#pragma once
#include "MagicControl/HardwareInventory.h"
#include <optional>
namespace mc {
void appendWindowsDetails(HardwareInventory& inventory, std::stop_token cancellation);
void appendWindowsDiagnosticDetails(HardwareInventory& inventory, std::stop_token cancellation);
// nullopt: Windows query failed; empty: this registry panel is not active.
std::optional<std::vector<InventoryRow>> activeDisplayRows(const std::string& panelId);
}
