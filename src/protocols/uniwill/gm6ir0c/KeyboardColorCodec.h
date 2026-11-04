#pragma once
#include <array>
#include <cstdint>

namespace mc::gm6 {
using KeyboardRgb = std::array<std::uint8_t,3>;
namespace keyboard_codec_detail {
// Exact active GM6 GCUService1.0.2.70 SingleZone.m_color_cell.Take(Count-1).
// Recognition compares UI RGB, NOT the table's R_Level/G_Level/B_Level data.
// White (entry30) is excluded here and handled by SetRGBColor's special case.
inline constexpr std::array<KeyboardRgb,29> paletteUi{{
    {{255,0,0}},{{255,50,0}},{{255,80,0}},{{145,60,0}},{{255,102,0}},
    {{255,128,0}},{{255,180,0}},{{150,128,2}},{{255,204,0}},{{204,225,0}},
    {{120,255,0}},{{60,115,18}},{{0,255,0}},{{0,255,80}},{{0,255,180}},
    {{60,125,135}},{{0,255,255}},{{0,180,255}},{{0,80,255}},{{0,35,102}},
    {{0,0,255}},{{80,0,255}},{{180,0,255}},{{110,45,100}},{{255,0,255}},
    {{255,0,180}},{{255,0,80}},{{180,5,0}},{{0,0,0}}
}};
constexpr std::uint8_t channel(std::uint8_t value,unsigned divisor) noexcept {
    const unsigned level=value/divisor;
    return static_cast<std::uint8_t>(level<1?1:level>50?50:level);
}
}
// Canonical UI RGB0..255 -> raw EC levels. Pure conversion only: this grants no
// board/transport access. Saved INI and actual EC RGB are ALREADY raw and must
// never pass through this conversion a second time.
// Active SingleZone.cs:154-156,756-824 and SetColorMode:1072, not its metadata
// level triplets. Native minimum1 and explicit zero-channel exceptions are
// intentional; matching a byte sequence does not prove its physical colour.
[[nodiscard]] constexpr KeyboardRgb nativeKeyboardColorLevels(const KeyboardRgb& rgb) noexcept {
    bool recognized{};
    for(const auto& entry:keyboard_codec_detail::paletteUi)if(entry==rgb){recognized=true;break;}
    const unsigned gbDivisor=recognized?5:6;
    KeyboardRgb raw{keyboard_codec_detail::channel(rgb[0],5),
        keyboard_codec_detail::channel(rgb[1],gbDivisor),keyboard_codec_detail::channel(rgb[2],gbDivisor)};
    if(rgb==KeyboardRgb{255,255,255})return {50,40,40};
    if(rgb==KeyboardRgb{255,0,80})return {50,0,6};
    if(rgb==KeyboardRgb{255,50,0})return {50,5,0};
    if(rgb==KeyboardRgb{255,80,0})return {50,10,0};
    if(rgb==KeyboardRgb{145,60,0})return {45,13,0};
    if(rgb==KeyboardRgb{255,102,0})return {40,10,0};
    if(rgb==KeyboardRgb{255,128,0})return {40,15,0};
    if(rgb==KeyboardRgb{255,180,0})return {40,25,0};
    if(rgb==KeyboardRgb{150,128,2})return {35,20,0};
    return raw;
}
}
