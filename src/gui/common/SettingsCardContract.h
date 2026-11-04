#pragma once
#include <array>
namespace mc::gui::ui {
// Logical IDs: battery=0, power=1, lighting=2. One order for both native pages.
inline constexpr std::array<int,3> settingsCardOrder{1,2,0};
}
