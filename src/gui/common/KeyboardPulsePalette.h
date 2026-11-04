#pragma once
#include "MagicControl/KeyboardBacklight.h"
#include "MagicControl/KeyboardPulsePolicy.h"
#include <array>
#include <string_view>
namespace mc::gui::ui {
// ПРАВИЛО ВЛАДЕЛЬЦА25.10.2026: порядок отображения цветов в интерфейсе НЕ МЕНЯЕМ.
// В пульсации кнопки всегда Белый -> Красный -> Зелёный -> Синий.
// Исправляем только вывод на клавиатуру: model-specific preset/команду для
// каждой существующей кнопки. Нельзя переставлять swatches ради другой прошивки.
using mc::KeyboardPulsePalette;
// Final owner instruction: never show the custom picker in Pulse, even HID.
inline constexpr bool customKeyboardColorVisible(int effect){return effect!=1&&effect!=3;}
// WUJIE: owner-confirmed firmware order1=white,2=red,3=green,4=blue.
using mc::wujiePulsePalette;
// GM6 owner hardware correction25.10.2026: old UI sent4/1/2/3 and physically
// produced blue/white/red/green. Thus1=white,2=red,3=green,4=blue. Keep the
// shared White/Red/Green/Blue button order; OEM XAML artwork is not hardware proof.
using mc::gm6ObservedPulsePalette;
// GM5 retains its existing OEM-only table. Do not propagate the exact GM6 user's
// correction to another board. L16 remains numbered until physically identified.
using mc::gmPulsePalette;
using mc::unknownGmPulsePalette;
using mc::keyboardPulsePalette;
}
