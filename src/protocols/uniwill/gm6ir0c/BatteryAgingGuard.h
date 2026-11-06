#pragma once
#include <cstdint>

namespace mc::gm6 {
// Pure EC 1.12 Project25 checks. Board/BIOS/driver and ownership are admitted
// separately; these predicates alone never open a transport or grant a writer.
inline constexpr bool agingEcIdentity(std::uint8_t version,std::uint8_t project,
        std::uint8_t capability,std::uint8_t ready,bool requireBattery) noexcept {
    return version==0x4c&&project==25&&capability!=0xff&&(capability&0x40)&&
        (!requireBattery||(ready!=0xff&&(ready&6)==6));
}
inline constexpr bool agingI2ecConfiguration(std::uint8_t baseHigh,std::uint8_t baseLow,
        std::uint8_t enabled) noexcept {
    return baseHigh==6&&baseLow==0x80&&enabled!=0xff&&(enabled&1);
}
inline constexpr bool agingExternalPower(std::uint8_t ready) noexcept {
    return ready!=0xff&&(ready&1);
}
// Firmware's two exposure accumulation dividers reset below60. In particular,
// an all-FF private memory window must never be accepted as a live baseline.
inline constexpr bool agingExposureDividers(std::uint8_t first,std::uint8_t second) noexcept {
    return first<60&&second<60;
}
// Exact SKU18 firmware chooses one of two nonzero private table pointers.
// This rejects an inaccessible private I2EC window that returns all zero/FF.
inline constexpr bool agingPrivateWindow(std::uint8_t high,std::uint8_t low,std::uint8_t sku) noexcept {
    const auto pointer=static_cast<std::uint16_t>((unsigned(high)<<8)|low);
    return sku!=0xff&&pointer==(sku==0x15?0x5d60:0x5d48);
}
inline constexpr bool agingSku18(std::uint8_t id,std::uint8_t selector) noexcept {
    return id!=0xff&&selector!=0xff&&(selector&1)&&(id&0x3f)==0x18;
}
}
