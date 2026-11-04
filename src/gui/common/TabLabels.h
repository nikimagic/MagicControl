#pragma once
#include <array>
#include <string_view>

namespace mc::gui::ui {
// ПРАВИЛО ВЛАДЕЛЬЦА (20.10.2026): названия вкладок в шапке фиксированы.
// Не переводить их на RU/EN/简体中文/繁體中文 и не менять по модели ноутбука.
// Обе ОС используют этот список; локализуются только подписи внутри страниц.
inline constexpr std::array<const char*,7> kHeaderTabTitlesUtf8{
    "FANs", "SETs", "KEYs", "INFO", "GPU Nvidia", "GPU Radeon", "OvClock"};
inline constexpr std::array<const wchar_t*,7> kHeaderTabTitlesWindows{
    L"FANs", L"SETs", L"KEYs", L"INFO", L"GPU Nvidia", L"GPU Radeon", L"OvClock"};
constexpr bool isFixedTabLabel(std::string_view source) noexcept {
    for(const auto* title:kHeaderTabTitlesUtf8)if(source==title)return true;
    return false;
}
} // namespace mc::gui::ui
