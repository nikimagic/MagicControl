#pragma once
#include "MagicControl/HardwareInventory.h"
#include <functional>
namespace mc::windows {
// Fixed explicit LC policy notification in the existing bounded helper.
// Predicate reads are exact HYDROC-only; this helper never writes EC or BLE.
bool invokeHydrocLppCondition(bool expectedPresence,const std::function<void(const InventoryRow&)>&);
}
