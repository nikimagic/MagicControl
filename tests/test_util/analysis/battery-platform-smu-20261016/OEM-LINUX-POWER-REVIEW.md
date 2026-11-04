# Дополнительные пути мощности: OEM и Linux Uniwill

16.10.2026. Только чтение исходников и сохранённых дампов, без аппаратных записей.

## OEM RAM Fan 1.5

`MyControlCenter/PowerModeEvent.cs:33`: StatusChange обновляет ACLineStatus и
вызывает PowerStatusChange. `MyFanManager_RamFan1p5.cs:1105` вызывает Init(false),
который RefreshCurrentProfile/SetUserProfile применяет заново (4574–4602).
Это комбинированный OEM путь; нельзя переносить его целиком, он меняет FANs.

Кнопки профилей дополнительно сохраняют `NvramVariable.SetFwVars("PowerMode", n)`:
Office=0, Balanced=1, Turbo=2, Custom=3 (1275–1344). Наш runtime-профиль в 751
переключается, но этот путь сохранения PowerMode отдельно не перенесён.
В DefaultTool NvramVariable.cs:484/519 PowerMode вызывает SetFwBufferTesting,
который пишет структуру через WriteUefi. Это не найденный EC commit-bit;
связь с текущими SMU-лимитами не доказана, слепая запись всей структуры недопустима.

Кандидаты в MyFanManager_RamFan1p5.cs:

| Поле | Метод и смысл | Ограничение доказательства |
|---|---|---|
| 7A5.7 | SetPowerStatus, HIGH_POWER | В просмотренном RAM1p5 файле есть определение, вызовов не найдено |
| 7A6.1 | SetOverBoostByDynamicTemp: 0=включить, 1=выключить | Не 7A5.1; вызовов в этой ветке не найдено |
| 753/754 | SetCpuVrmCurrentLimit | AMD Custom, SPL/SPPT >=75; не объясняет сам по себе лимит Turbo 35 |
| 786.7 + payload | SetCpuTccOffset | AMD температурная цель, не power-limit apply |
| PowerMode в UEFI | Сохранение выбранного режима | Есть активные вызовы; аппаратное действие при AC/DC отдельно не проверено |

В сохранённых дампах 15.10.2026: 7A5=0, 7A6=0, 753/754=0, 786=0,
в том числе в дампе подключения 100 Вт. Это исторические значения, не текущая
телеметрия; не считать их доказательством нужного нового флага.

## Linux

Проверены текущие первичные источники:

- https://github.com/torvalds/linux/blob/master/drivers/platform/x86/uniwill/uniwill-acpi.c
- https://github.com/tuxedocomputers/tuxedo-drivers/blob/main/src/uniwill_keyboard.h
- https://github.com/tuxedocomputers/tuxedo-drivers/blob/main/src/uniwill_interfaces.h
- https://github.com/tuxedocomputers/tuxedo-drivers/blob/main/src/tuxedo_io/tuxedo_io.c

Linux подтверждает обозначения HIGH_POWER=7A5.7, OVERBOOST=7A5.4,
OVERBOOST_DYN_TEMP_OFF=7A6.1, priority=7CC.7. В TUXEDO приоритет ограничен
поддерживаемыми моделями; описано удаление функции из новых прошивок.
Наличие capability 742.5 недостаточно. Сходные современные AMD DMI используют
набор без приоритета; это не доказательство поддержки нашей конкретной платы.

TUXEDO ручные TDP пишутся в 783–785; некоторые модели требуют Custom 727.6
и профиля Overboost, PL4 масштабируется по поддержке. Наш 727.6 уже установлен,
Turbo=751.4 уже выбран, а Manual аппаратно принимает overrides.
Новых обязательных для нашего устройства флагов из этого не следует.

## Что пока не установлено

Отдельного подтверждённого «включить полную мощность AC» флага не найдено.
65 Вт USB-C может иметь самостоятельный firmware-бюджет. Для разделения
нужны одинаковые условия 65/100 Вт и чтение PowerMode/SmartAPC, затем проверка
конкретного кандидата. Не включать HIGH_POWER/OVERBOOST на основании имён.
Платформа 7C3 уже имеет доказанный DC-побочный эффект; AC 65 Вт от неё не изменился.
