// MagicControl - startup board identification.
//
// The laptop model is read once from the operating system's SMBIOS view and the
// EC is asked for its three identity bytes. Only when those bytes match the
// values proven on the reference board does the core allow writes, because a
// wrong-board write can change firmware settings that belong to the OEM tool.
#pragma once

#include <optional>
#include <string>

#include "MagicControl/EcAccess.h"

namespace mc {

/// Legacy identifiers for the first verified profile. DeviceRegistry owns
/// selection; these constants must not be used as a global support predicate.
inline constexpr const char* kReferenceModelId = "wu-jie-15-xpro";
inline constexpr const char* kReferenceManufacturer = "MECHREVO";
inline constexpr const char* kReferenceBaseBoard = "WUJIE Series-X5SP4NAG";

/// Reads the SMBIOS identity from the operating system.
///
/// Windows: HARDWARE\DESCRIPTION\System\BIOS in the registry.
/// Linux:   /sys/class/dmi/id/*.
/// Returns an empty descriptor, never throws, when the data is unavailable.
[[nodiscard]] BoardDescriptor readBoardDescriptor();

/// Runs the full startup check: SMBIOS identity plus the three EC identity
/// bytes. Never throws; a failure is reported in the result, which keeps write
/// access disabled.
[[nodiscard]] BoardCheck runBoardCheck(EcAccess& access);

/// Formats a board check as one diagnostic line for logs.
[[nodiscard]] std::string describeBoardCheck(const BoardCheck& check);

}  // namespace mc
