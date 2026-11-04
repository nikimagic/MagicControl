# GM6IR0C: ревизия мониторинга, 18.10.2026

## Доказательства

Пользовательский `logs (4).zip`, SHA256
`cf05b1c92f0f88c76e76fec71506f64816e8997fb46383517525b95b4936afd4`.
Версия0.1.50,422 события,41 power sample. Сессия19:14:26–19:27:51 UTC;
снимки power sample охватывают первые≈82 секунды. Это не непрерывная запись всей сессии.
PawnIO установлен и running=true, identity GM6IR0C/ProjectID25 подтверждена.

| Показание | Результат0.1.50 | Ревизия |
|---|---|---|
| Intel Package DTS | 41/41,53–66°C | Работает; TjMax отдельно от текущей температуры |
| Intel Package RAPL | 40/41,11.834–16.776Вт | Первый снимок без мощности ожидаем: нужен интервал энергии; время фиксируется после чтения счётчика |
| NVIDIA NVML | 41/41,48°C,20.041–20.576Вт | Данные работают; неоднозначный выбор нескольких одинаковых GPU отклоняется, устаревшая VRAM очищается |
| CPU RPM/PWM | 2055–2085RPM,30% | Работает, источник UWACPIDriver |
| GPU RPM/PWM | 1957–2213RPM,30% | Работает, отдельный OEM low-byte0x46B |
| DDR5 DIMM | Прочерки | Intel SMBus отсутствовал в реализации. Добавлен отдельный I801/SPD5118 reader, требуется запуск на GM6IR0C |
| SSD | Прочерк | Добавлен стандартный NVMe SMART fallback; температурный descriptor принимает доступный ненулевой индекс и0°C; требуется проверка на KINGSTON |
| Батарея: заряд/напряжение/мощность | 100%,12.385–12.408V,0Вт | Работает через Battery Class; это простой от сети |
| Температура батареи | null во всех41 снимках | Ранее код молча терял ошибку. Добавлены raw, источник и точный Win32 status запроса; причина на целевом ПК пока неизвестна |
| Циклы/ток батареи | Не зарегистрированы/прочерки UI | Нельзя объявить датчик отсутствующим только по старому логу |
| Intel PL1/PL2/PL4=0 | EC overrides, не измеренные лимиты | На недоступных для редактирования полях теперь «Авто», не0Вт |
| KEYs Windows/Fn/USB/AC | Generic EC недоступен | Для GM6IR0C эти функции пока не подключены. Сообщение больше не выдаёт это за отказ всего EC |
| FANs «Не применена» при Default | Generic draft сравнивался с чужим профилем | Подпись GM берётся из прочитанного OEM режима; Custom график явно остаётся черновиком |
| EC power diagnostics | Ложное обобщение unavailable | Generic X5-карта помечена неприменимой; typed GM данные остаются в канале gm6ir0c |

## Батарея и ACPI

Из исходного исследовательского архива локально декомпилирован
`build/research-gm6ir0c/samples/DSDT.aml` (BIOS N.1.05MRO09).
`BAT0._BIF` возвращает ёмкость/напряжение, `_BST` — состояние/скорость/ёмкость/напряжение.
В ECXP (base ECMA+0x400) есть CYCN offset0xA6; наличие поля не является
физической проверкой значения. `XTP0` рядом с `_BST` не назван температурой
и не получает такое назначение по сходству имени (trip point не температура).
Однозначного battery-temperature поля/метода в разобранном DSDT не найдено.
Декомпиляция имеет unresolved external methods: это не доказательство отсутствия
температуры во всей прошивке или других SSDT.

Следующий лог новой сборки запишет `battery.temperature_status`,
`temperature_raw`, `temperature_source`, массивы `memory`/`disks` с ошибками.
Текущий null не позволяет отличить ERROR_NOT_SUPPORTED от ошибки/невалидного raw.
Нельзя переносить EC-регистры батареи X5SP4NAG на GM6IR0C или выдавать ACPI
Thermal Zone/CPU температуру за батарейную.

## Реализованный Intel RAM путь

Официальный подписанный `SmbusI801.bin`0.2.11, SHA256
`a0f7d066e7efda28c0e754c1f52dbd8dc280d388ba8575b514a80cb67490530c`.
Только точный SMBIOS GM6IR0C + GenuineIntel, --no-ec запрещает этот путь.
Общий mutex `Global\Access_SMBUS.HTP.Method`; проверка Intel vendor identity.
Ограниченный поиск SPD5118 на0x50–0x57, максимум два датчика; кэш опроса5с,
discovery60с, ошибки модуля повторяются через30с. ByteData/WordData только read.
Type0x1851, thermal capability MR5, config MR26; MR49/50 signed11bit /4°C.
Страницы NVM/конфигурация/включение датчика не меняются. Адрес датчика не
приравнивается к SMBIOS locator: показывается DDR5/SMBus address, без чужого S/N.
Linux SPD5118 также больше не приписывает температуре серийник по порядку SMBIOS.

## Границы

На GM6IR0C доступа SSH нет. Добавленные I801/SMART пути и новый battery status
требуют следующего запуска владельцем; сборка и декодер-тесты не доказывают
появление физических показаний. OEM GCUService остаётся работающим и блокирует
аппаратные записи. Ревизия не снимает эту блокировку и не пишет EC/MSR/SMBus data.
Батарейные лимиты и RGB GM6IR0C пока не подключены. Дизайн/скролл/геометрия общие.

Источники: Microsoft BATTERY_QUERY_INFORMATION (BatteryTemperature — 0.1K),
https://learn.microsoft.com/en-us/windows/win32/power/battery-query-information-str ;
Windows NVMe Get Log Page:
https://learn.microsoft.com/en-us/windows/win32/fileio/working-with-nvme-devices ;
https://github.com/namazso/PawnIO.Modules/tree/0.2.11 ;
https://github.com/torvalds/linux/blob/master/drivers/hwmon/spd5118.c .

## Программные проверки

Отдельный test_sensor_decoders:72 проверки прошли под GCC15/WSL и MSVC/Windows.
Проверены0/FFFFFFFF/границы BatteryTemperature, SPD5118 signed11bit, NVMe
буфер/offset/length/unknown/sentinel, запасной индекс storage-temperature и0°C,
сохранение источника прошлого показания батареи и отображение адреса вместо S/N.
Тестовый executable не подключает платформенный бэкенд и не обращается к оборудованию.
