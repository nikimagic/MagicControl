#pragma once
#include "MagicControl/EcFieldObservation.h"
#include <cstdint>
#include <optional>
#include <string_view>

namespace mc {
class EcAccess;
namespace windows { class NativeBatteryController; }
namespace gui {

struct EcLoggerRequest {
    std::uint16_t address{};
    std::uint8_t value{}, expected{};
    bool writing{};
};
enum class EcLoggerInputError { None, InvalidHex, ObservationRequired };
struct EcLoggerPrepared { std::optional<EcLoggerRequest> request; EcLoggerInputError error{EcLoggerInputError::None}; };
// Parse and compare against the caller's observation only. Existing backend
// admission, ownership, fresh comparison and readback are still mandatory.
EcLoggerPrepared prepareEcLogger(std::string_view address,std::string_view value,bool writing,
                                 const std::optional<EcByteObservation>& observation);

// Caller owns the existing DeviceOperation admission and worker lifetime.
// On a changed EC byte, observation is populated before the exception, so
// the UI can present the actual value without retrying or writing again.
void executeEcLogger(const EcLoggerRequest&, windows::NativeBatteryController*,
                     EcAccess*, std::optional<EcByteObservation>& observation);

}
}
