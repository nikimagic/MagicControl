#pragma once
#include <string>
namespace mc::linux_platform {
// Explicit user Sc only. Exactly one active internal panel, existing rotation
// plus180 degrees, native readback. No XWayland output fallback on Wayland.
std::string toggleBuiltInDisplayRotation();
}
