#pragma once
#include "MagicControl/GpuPresentation.h"
namespace mc::windows {
// Active OS display source/GDI routes only; no device creation or MUX setter.
std::vector<GpuDisplayRoute> gpuDisplayRoutes(const std::vector<GraphicsAdapter>& adapters);
}
