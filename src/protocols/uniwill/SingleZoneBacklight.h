#pragma once
#include <cstdint>
// Independently traced in active GM6IR0C SingleZone OEM code and verified X5
// sources. Shared layout does not grant another board access. Every entry goes
// through its exact profile/identity/capability guard. Never write whole shared
// bytes:7C5.bit7 belongs to fans;767.bit4 belongs to USB, bit1 to lightbar.
// Explicit level0/OFF stops rainbow767.7 and pulse7C5.low3 before power-disable.
// Native idle powerDisabled-only and exact temporary snapshot restore retain
// effects. All changes use the shared journal/readback/owned rollback.
namespace mc::singlezone {
inline constexpr std::uint16_t support=0x766,level=0x78C,effect=0x767,pulse=0x7C5;
inline constexpr std::uint16_t red=0x769,green=0x76A,blue=0x76B,defaultRed=0x76C;
inline constexpr std::uint8_t rgbSupport=4,levelMask=0xE2,levelStrobe=0x10;
inline constexpr std::uint8_t rainbow=0x80,pulseMask=7,colorStrobe=0x20;
}
