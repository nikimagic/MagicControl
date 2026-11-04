#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace mc {
enum class SensorTimestampOrigin { Unknown, SoftwareObservation };
// Additional source readings are diagnostic data, never control inputs.
// IDs are physical paths/API identifiers; unknown values stay null.
struct SensorReading {
    std::string device, metric, unit, source, status;
    std::optional<double> value, raw;
    std::uint64_t sampledAtMs{};
    // Timestamp origin is supplied by the reader, not inferred from its name.
    SensorTimestampOrigin timestampOrigin{SensorTimestampOrigin::Unknown};
    friend bool operator==(const SensorReading&, const SensorReading&) = default;
};
}
