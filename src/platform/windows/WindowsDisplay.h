#pragma once
#include <string>
#include <stop_token>
#include <vector>
#include <windows.h>
namespace mc::windows {
struct ActiveDisplayConfiguration {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG error{ERROR_SUCCESS};bool complete{};std::string status;
};
// Shared current (not persisted/preferred) CCD getter. Bounded hotplug retries.
ActiveDisplayConfiguration readActiveDisplayConfiguration(std::stop_token token={});
std::string displayDiagnosticJson(std::stop_token token={});
[[nodiscard]] std::string toggleBuiltInDisplayRotation();

}
