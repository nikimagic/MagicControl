#pragma once
#include <filesystem>
#include <span>
#include <string_view>

namespace mc::windows {
// Original reviewed RCDATA bytes. No file, device, service or installer is opened.
std::span<const char> embeddedRuntimeBytes(std::string_view group,std::string_view name);
// Location only: does not create/extract the cache.
std::filesystem::path embeddedRuntimeCachePath(std::string_view group);
// Lazily extract/verify/pin a group in the protected system data cache.
// Never installs/starts a kernel driver and never writes beside the EXE.
std::filesystem::path embeddedRuntimeDirectory(std::string_view group);
}
