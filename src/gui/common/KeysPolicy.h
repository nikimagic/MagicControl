#pragma once
#include <array>
#include <cstddef>
#include <string_view>
#include "MagicControl/QuickSwitch.h"
#include "MagicControl/KeysLifecycle.h"
#include "MagicControl/KeysBackendPolicy.h"

namespace mc::gui::ui {
using KeyAction=mc::KeyAction;
inline constexpr std::size_t keyRowCount=mc::kKeyCount, keyQuickRowCount=6;
inline constexpr int microphoneRow=static_cast<int>(KeyAction::Microphone);
inline constexpr std::array<const char*,keyRowCount> keyTitles{
    "Блокировка клавиши Windows","Отключить экранное меню","Блокировка клавиши Fn",
    "Питание USB при выключении и гибернации","Автовключение при подаче питания",
    "Блокировка клавиши Copilot","Wi-Fi","Bluetooth","Веб-камера","Сенсорная панель","Микрофон"};
using mc::KeysBackend;
using mc::keysBackend;
}
