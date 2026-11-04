#pragma once
#include <array>
#include <cstddef>
namespace mc::gui::ui {
inline constexpr std::size_t kBiosActionCount = 8;
// Stable action IDs; visual and keyboard order are shared by Windows/Linux.
inline constexpr std::array kBiosPrimaryActions{1, 0, 5, 6, 7};
inline constexpr std::array kBiosActionTabOrder{1, 0, 5, 6, 7, 2, 3, 4};
inline constexpr char kInformationCaption[] = "INFO · сведения о ноутбуке";
inline constexpr char kDiagnosticDumpCaption[] = "Снять дамп EC";
inline constexpr char kBiosRomDumpCaption[] = "Снять дамп BIOS";
inline constexpr char kVbiosRomDumpCaption[] = "Снять дамп VBIOS";
inline constexpr char kVbiosRomDumpNote[] = "NVIDIA: VBIOS и полный EEPROM, по два чтения. Ёмкость, SHA256 и raw compare; EEPROM до 16 МиБ. Нужны права администратора/root.";
inline constexpr char kPawnIoInstallCaption[] = "Установить драйвер PawnIO";
inline constexpr char kPawnIoInstalledCaption[] = "Драйвер PawnIO установлен";
inline constexpr char kPawnIoWindowsNote[] = "PawnIO устанавливается по кнопке: извлечение встроенного официального установщика и запуск мастера установки. После установки перезапустите MagicControl.";
inline constexpr char kPawnIoLinuxNote[] = "PawnIO — драйвер Windows. В Linux используются доступные системные драйверы; установка PawnIO здесь не требуется.";
}
