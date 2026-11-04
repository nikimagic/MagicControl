#pragma once
#include "Monitoring.h"

namespace mc {
// Zen SMN 0x59800, matching k10temp get_raw_temp(): 0.125 C per step.
// The range flags select -49 C. This is not a guessed CPU-model offset.
// https://github.com/torvalds/linux/blob/master/drivers/hwmon/k10temp.c
inline std::optional<double> decodeZenTctl(std::uint32_t raw) {
    if (raw == 0 || raw == UINT32_MAX) return {};
    double celsius = (raw >> 21) * 0.125;
    if ((raw & 0x80000) || (raw & 0x30000) == 0x30000) celsius -= 49;
    return celsius > 0 && celsius < 150 ? std::optional<double>(celsius) : std::nullopt;
}

// Each snapshot starts with the driver value. An unavailable SMN read retains
// that value and its source; it never reuses a previous Tctl reading.
inline void applyZenCpuTemperature(MonitoringSnapshot& sample, std::optional<double> celsius) {
    if (!celsius) return;
    auto& cpu = sample.processors[0];
    cpu.celsius = cpu.tctlCelsius = celsius;
    cpu.temperatureSource = "PawnIO · Tctl/Tdie";
}
} // namespace mc
