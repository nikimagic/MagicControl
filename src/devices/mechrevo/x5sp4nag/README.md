# MECHREVO Wujie 15X Pro — X5SP4NAG

Единственный подтверждённый профиль аппаратного управления.
SMBIOS: MECHREVO / WUJIE Series-X5SP4NAG. EC identity: 82/72/26.
Название в шапке и меню: MECHREVO · WUJIE X5SP4NAG. Прежнее торговое имя
Wujie 15X Pro сохранено в DeviceProfile::alternativeName; оно не участвует
в распознавании платы. Слово Series удалено только из отображаемой подписи.
Протокол: src/protocols/uniwill/x5sp4nag.
EC-транспорт: Windows Uniwill ACPI. Мониторинг SMU: Windows PawnIO, PM 0x5D0009.
Linux EC пока недоступен.

Контракты: src/platform/windows/FAN_MODES.md, BATTERY.md и src/core/src/POWER_MODES.md.
Особенности платы и Windows API находятся в windows/. Общие интерфейсы совместимости
сохранены в src/platform/windows/*.h. При перемещении алгоритмы не изменялись.

## Подсветка: владение и временная индикация20.10.2026

Команды SingleZone используют общий KeyboardBacklight и read-only admission
LightingOwnershipWindows: активное или неизвестное состояние проверенных OEM
служб/процессов запрещает запись. Этот snapshot не является эксклюзивной
блокировкой чужого OEM; служба автоматически не останавливается подсветкой.

ConfigurationRestore согласует собственный flash/restore с общим циклом через
KeyboardLightingPause и подтверждает свежий итог независимо от ошибки APU.
Adapter flash не сохраняет настройки яркости, не пробуждает явный Off/idle blank
и возвращает только собственное временное RGB/эффектное состояние. Чужая смена
отменяет возврат старого снимка; Fn-уровень сохраняется, свежий78C.1 не стирается.
Selected level в78C.bits7:5 отделён от временного power-disable в78C.1.

Это не подключение аппаратного inactivity timeout.741.0 остаётся общим APP/fan
gate;741.2 и6FA не используются как writable время. Эталонный UI/скролл сохранён.

26.10.2026: в извлечённом EC найден native handler bank2 BB34, счётчик08A5/08A6,
blank741.2 и фиксированные пороги platform4=30 / other=600. APP741.0=1
сбрасывает этот обработчик. Это не разрешение снимать APP или менять платформу:
они используются батареей/FAN/APU. Подробности и границы времени — в
research/keyboard-ec-autooff-20261026/REPORT.txt. По решению владельца весь блок
«Авто Выкл» скрыт в SETs у всех моделей; core API/сохранённые настройки сохранены.
