#pragma once
#include <optional>
#include <string>
#include <vector>

namespace mc::windows {
// Explicit command-line entry, used before GUI ownership and restoration.
// Unrecognized arguments return no result and must not access hardware.
std::optional<int> runBiosDumpHelper(const std::vector<std::string>& arguments);
}
