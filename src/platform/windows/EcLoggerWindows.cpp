#include "EcLoggerWindows.h"
#include "NativeBatteryWindows.h"
#include "UniwillControlOwnerWindows.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/EcByteEditor.h"
#include "MagicControl/EcHexInput.h"
#include "MagicControl/Logging.h"
#include <format>
#include <stdexcept>
#include <vector>

namespace mc::gui {
EcLoggerPrepared prepareEcLogger(std::string_view addressText,std::string_view valueText,bool writing,
                                 const std::optional<EcByteObservation>& observation){
    const auto address=parseEcHex(addressText,ecEditorMaximumAddress);
    const auto value=parseEcHex(valueText,255);
    if(!address||(writing&&!value))return {{},EcLoggerInputError::InvalidHex};
    if(writing&&(!observation||observation->address!=*address))return {{},EcLoggerInputError::ObservationRequired};
    return {EcLoggerRequest{*address,static_cast<std::uint8_t>(value.value_or(0)),
        static_cast<std::uint8_t>(observation?observation->value:0),writing},EcLoggerInputError::None};
}
void executeEcLogger(const EcLoggerRequest& request,
                     windows::NativeBatteryController* nativeBattery,
                     EcAccess* ec,
                     std::optional<EcByteObservation>& observation) {
    const auto [address, value, expected, writing] = request;
    requireEcEditorAddress(address, writing);
    const auto profile = deviceProfile(readBoardDescriptor()).id;
    const auto mask = writing ? ecEditorChangeMask(profile, address, expected, value) : 0;
    logMessage("battery", std::format("EC_EDITOR requested address=0x{:03X} write={} value=0x{:02X}", address, writing, value));
    if (nativeBattery) {
        if (writing) nativeBattery->writeEditorByte(address, expected, value);
        observation = observeEcField(profile, address, [nativeBattery](auto base, unsigned count) {
            return nativeBattery->readEditorBytes(base, count);
        });
    } else if (ec) {
        if (!ec->boardVerified()) throw std::runtime_error("EC identity не подтверждена");
        const EcAccess::Transaction transaction(*ec);
        const auto observe = [&] {
            return observeEcField(profile, address, [ec](auto base, unsigned count) {
                std::vector<std::uint8_t> bytes;
                for (unsigned i = 0; i < count; ++i) bytes.push_back(ec->readStable(static_cast<std::uint16_t>(base + i)));
                return bytes;
            });
        };
        if (writing) {
            const auto owner = windows::uniwillControlOwner();
            if (!owner.empty()) throw std::runtime_error("Управление занято: " + owner);
            if (ec->readStable(address) != expected) {
                observation = observe();
                throw std::runtime_error("EC изменился: сначала перечитайте адрес клавишей Enter");
            }
            if (mask) ec->updateBitsCommand(address, static_cast<std::uint8_t>(mask), value);
        }
        observation = observe();
    } else throw std::runtime_error("Raw EC для этой модели недоступен");
    const auto actual = observation->value;
    logMessage("battery", std::format("EC_EDITOR observed address=0x{:03X} value=0x{:02X} write={} confirmed={}", address, actual, writing, !writing || actual == value));
    if (writing && actual != value) throw std::runtime_error("EC не подтвердил запись байта; повтор и автоматический откат не выполнялись");
}
}
