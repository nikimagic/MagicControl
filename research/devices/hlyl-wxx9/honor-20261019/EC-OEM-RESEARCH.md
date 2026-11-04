# Honor HLYL-WXX9: EC, OEM-софт и драйверы

Дата исследования: 19.10.2026. Проверяемая машина: LAPTOP-8837V73M,
HUAWEI HLYL-WXX9 / HLYL-WXX9-PCB, BIOS1.09, IT8987.
Доступ: SSH aniki@192.168.10.112. Исследование бинарников — офлайн.

## Результат

Для штатных датчиков, Fn/Turbo и зарядки не нужен новый сторонний EC-драйвер:
установленный OEM-стек использует Microsoft ACPI-WMI. Низкоуровневые I2EC-функции
подсветки и вентиляторов остаются отдельным путём через наш существующий PawnIO
LpcIO. Наличие WMI не предоставляет произвольное чтение/запись всей памяти EC.

В публичных проектах найдены несколько других EC-драйверов, но ни один из
изученных новых драйверов не подтверждён для этой платы и BIOS. Готовый новый
универсальный Windows-драйвер Huawei для полного дампа HLYL не найден.
Это итог проведённого поиска, а не утверждение об отсутствии такого драйвера
во всех репозиториях интернета.

## 1. Что действительно установлено и загружено

PC Manager(Multi-screen Collaboration and Official Driver), версия11.1.6.31,
Huawei Device Co., Ltd. Основной каталог: `C:\Program Files\Huawei\PCManager`.
Файловая версия его аппаратных модулей —11.0.0.0; это не номер пакета11.1.6.31.

Работают службы MBAMainService/MateBookService.exe, HwPCCoreService/BasicService.exe,
HiviewService и LCD_Service. В загруженных модулях MateBookService и
MBAMessageCenter подтверждены HardwareHal.dll и WmiUtil.dll из PCManager.
BasicService использует копии из собственного каталога.

| Файл PCManager | Байт | SHA-256 |
| --- | ---: | --- |
| WmiUtil.dll | 112840 | AE4595E83180F4012BCD478730E58DD358AA5190462594532053AF551ED8FC7B |
| HardwareHal.dll | 523976 | B8398B5E6C2EB6823BE323BC435B06F7D81E5E5CBBAE915A975A9F912D483B2B |
| HwDriverAdapter.dll | 338120 | 645CCABE290B2FFC38624A3C63FC6748BFC7F045306E94E4F1B80558AFD819B6 |
| MateBookService.exe | 580296 | AD0718816B481CE5731533A59A91A7DD122822A12167A3BCE9D044DF3D885A98 |

Подписи выбранных OEM EXE/DLL проверены на Honor: Valid, Huawei Device Co., Ltd.
Полный манифест23 выбранных бинарников/INF: `oem/manifest.json`; локальные копии
проверены по SHA-256. Это исследовательские копии, не файлы для нашего релиза.

Полные хэши копий WmiUtil/HardwareHal/ProductAdaptMgr из BasicService отличаются,
но хэши всех соответствующих PE-секций совпали с PCManager/HwLcdEnhancement.
Нельзя считать их другой реализацией лишь из-за общего хэша файла.
Сравнение: `ec-oem-analysis/section-hashes.json`.

Привязка устройства на машине:

```
ACPI\PNP0C14\HWMI
  service: WmiAcpi
  INF: wmiacpi.inf
  provider: Microsoft
WMI instance: ACPI\PNP0C14\HWMI_0, Active=true
```

WmiAcpi.sys10.0.26100.1150 и ACPI.sys10.0.26100.1 имеют Valid-подписи Microsoft.
Для ACPI\PNP0C09 используется machine.inf/Microsoft, отдельное значение Service
пустое. Метаданные: `oem/acpi-bindings.json`, `microsoft-acpi-drivers.json`.
Такой механизм соответствует [образцу Microsoft ACPI-WMI](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/wmi-acpi-sample/).

Среди проверенных файлов Huawei обнаружены virtbus.sys, WFPDriver.sys,
dokan1.sys — виртуальная шина, сетевой фильтр и файловая система; они не
подтверждают доступ к EC. Среди служб драйверов с читаемыми файлами и метаданными
Huawei найден virtbus. HwOs2Ec не найден ни среди служб, ни в System32/drivers,
ни по имени в DriverStore/FileRepository. Полная область проверки сохранена
в `oem/os2ec-presence.json` и `oem/all-driver-services.json`.

## 2. Декомпиляция точных OEM-бинарников

Ghidra12.1.4: WmiUtil432 функции, HardwareHal1988, HwDriverAdapter1363,
MateBookService2690. Экспорт PE и журналы находятся в `build/honor-oem-analysis`.
Это автоматически восстановленный C-псевдокод, не оригинальные исходники:
типы/сигнатуры некоторых внутренних функций восстановлены неточно.
Ключевые29 фрагментов сохранены в `ec-oem-analysis/decompiled`.

Подтверждённая статическим анализом цепочка:

```
HotInterface::GetFan0Speed / GetFan1Speed / GetSensorTemperature
    HardwareHal.dll → HwSDK::BiosWmi (WmiUtil.dll)
    → ROOT\WMI / OemWMIMethod.OemWMIfun
    → Microsoft WmiAcpi / ACPI → AML BIOS → данные EC
```

HardwareHal RVA262E0/26310 вызывает GetFanSpeed с индексами0/1.
WmiUtil RVA2A90 формирует0802+индекс, проверяет транспорт и status0,
читает little-endian16bit из output[1..2]. Это готовые RPM без дополнительного
масштабирования. Ошибка OEM-обёртки может выглядеть как нулевые RPM:
HotInterface инициализирует значение нулём и не возвращает ошибку отдельно.
В MagicControl следует хранить статус чтения отдельно от значения0.

| Назначение | Команда | Формат / подтверждение |
| --- | --- | --- |
| Обороты | 0802, индекс0/1 в byte2 | status=byte0, RPM=byte1+256×byte2 |
| Температура | 0202, индекс в byte2 | status, знак, целые°C; WmiUtil RVA2760 |
| Напряжение / ток | 0902, индекс в byte2 | status, знак, величина LE16; RVA28F0 |
| Пороги зарядки | 1003 | нижний/верхний в byte2/3; RVA2C20 |
| Fn Lock | 0604 / 0704 | getter/setter; не новая находка управления |
| Turbo | 0E04 / 0F04 | getter/setter; ранее подтверждённый путь HLYL |
| WorkingMode | 1004 / 1104 | экспорт есть; не проверял запись, наличие не доказывает поддержку BIOS |
| Таймер подсветки | 1206 / 1106 | Get/SetKeyboardLightTime, RVA4D70/4EF0 |
| MemoryTrainingData | 1306 | не считать этим кодом яркость новой модели; RVA5BC0 |

В этой версии WmiUtil транспорт RVA6C70 сначала пробует SAFEARRAY64 байта
u8Input, при неудаче — u64Input через BSTR. Выход — u8Output256 байт.
GetBiosInfo — обёртка того же WMI, а не найденная функция полного RAM-дампа.

### HwOs2Ec: найдено упоминание, но не рабочий новый транспорт

В MateBookService действительно есть `\\.\HwOs2EcX64`.
Разобранные вызовы RVA8250/87D0/8E70 принадлежат BiosLogo:
чтение/копирование физической области логотипа, освобождение отображения,
запись логотипа. Это не доказательство наличия универсальных EC IOCTL.
Эти функции на ноутбуке не вызывались.

Исторический HwOs2Ec10x64.sys также описан в
[исследовании Microsoft](https://www.microsoft.com/en-us/security/blog/2019/03/25/from-alert-to-driver-vulnerability-microsoft-defender-atp-investigation-unearths-privilege-escalation-flaw/)
о CVE-2019-5241/5242. Случайные старые SYS из коллекций не являются заменой
нашего транспорта: имя и подпись сами по себе не подтверждают пригодность.

## 3. Свежие чтения Honor 19.10, без аппаратных setter-команд

`oem/getter-observation.json`,09:00:18 MSK:

| Чтение | Значение | Длительность |
| --- | ---: | ---: |
| Fan0 | 0 RPM | 40мс |
| Fan1 | 2321 RPM | 13мс |
| CPU | 53°C | 7мс |
| Батарея | 33°C | 7мс |
| Напряжение батареи | 8159мВ | 6мс |
| Ток батареи | 0мА | 8мс |

Все шесть ответов —256 байт, status0. Это последовательные, не атомарные
снимки;0 RPM одного вентилятора не доказывает неисправность/нужный множитель.
Вентиляторами в этом исследовании не управляли.

DSDT BIOS1.09 явно реализует GTMP только для индексов0,5,7,8,11,14.
Следующее чтение всех этих индексов (`oem/dsdt-temperature-observation.json`):

| Индекс | Поле DSDT | Значение |
| --- | --- | ---: |
| 0 | CTMP, CPU | 39°C |
| 5 | TSLO | 38°C |
| 7 | TNTC | 33°C |
| 8 | CNTC | 44°C |
| 11 | DNTC | 40°C |
| 14 | BTMP, батарея | 33°C |

Названия TSLO/TNTC/CNTC/DNTC не доказывают расположение датчика на памяти,
GPU или плате. Не переименовывать их в DIMM/SSD без доказательств. GTMP имеет
также ветку VTMM/VTM*: значение может подменяться firmware virtual temperature,
поэтому для общего случая это именно OEM-канал с указанным именем.
Linux Huawei-WMI содержит больше индексов для других моделей; перебирать их
вслепую на HLYL не требуется.

## 4. Сопоставление с GitHub и китайскими первичными материалами

Поиск выполнен на английском/китайском, в веб-поиске и публичном GitHub API.
Полные ответы API и README-кандидатов сохранены в `ec-oem-analysis`.
Часть выдачи — серверные iBMC, телефоны, облачный ECS и другие ложные совпадения.
Gitee-поиск не дал дополнительного первичного HLYL-драйвера; отсутствие в выдаче
не означает отсутствие проекта. Все рассмотренные утилиты изучались статически.

| Источник | Что полезно | Применимость |
| --- | --- | --- |
| [sermart1234/huawei-wmi-tool](https://github.com/sermart1234/huawei-wmi-tool) | Публичные DSDT HLYL-WXX9_1.6/1.8 | Найден именно HLYL; ниже сравнение |
| [baldandbrave/Huawei-WMI](https://github.com/baldandbrave/Huawei-WMI) | Ордилалы/коды WmiUtil | Совпадает с экспортами установленной11.0.0.0 |
| [aymanbagabas/Huawei-WMI](https://github.com/aymanbagabas/Huawei-WMI) | Linux-драйвер HWMI, RPM/температуры/зарядка | Реальный драйвер Linux; Windows SYS не предоставляет |
| [AceDroidX/HuaweiBatteryControl](https://github.com/AceDroidX/HuaweiBatteryControl) и [китайский разбор автора](https://blog.acedroidx.top/HuaweiBatteryControl/) | Разбор HardwareHal,64-байтного WMI и порогов | Подтверждает транспорт, не общий RAM-доступ |
| [shockwave-coder/matefan](https://github.com/shockwave-coder/matefan) | SFND1102, пример управления | Проверенная автором FLMH-XX2024, не наш HLYL |
| [limsaiful42-eng/huawei-matebook-fancontrol](https://github.com/limsaiful42-eng/huawei-matebook-fancontrol) | Китайский разбор FlemingH BIOS1.22, SFND | Другая прошивка; список12000 — запросы, не гарантированные RPM |
| [duny31030/matebook-fanctld](https://github.com/duny31030/matebook-fanctld) | Подробный китайский разбор KLVG-XX/M1010 | Другой EC-командный протокол и SFND |
| [Halhadus/matebook-d16-2024-fan-control](https://github.com/Halhadus/matebook-d16-2024-fan-control) | Linux/acpi_call для D16 | Не HLYL |
| [kiradzero/hfac](https://github.com/kiradzero/hfac) | Реальный Linux EC/hwmon-драйвер BRI-XX | RDER/WTER,0D20/0A0E; не карта HLYL |
| [Little-King2022/honor-fan](https://github.com/Little-King2022/honor-fan), [littleflit/honor-fan](https://github.com/littleflit/honor-fan) | Драйвер/mailbox DRA-XX2024, китайская документация | Nuvoton и другие MMIO-окна, не IT8987 HLYL |
| [sealeelike/Honor-FRI-H76-fan-controller-for-linux](https://github.com/sealeelike/Honor-FRI-H76-fan-controller-for-linux) | RDER/WTER,0A18/0A19 | Только FRI-H76, не переносить |
| [mark-herbert42/art14-fan-daemon](https://github.com/mark-herbert42/art14-fan-daemon) | WTER/другая карта Art14 | Другой Intel Honor2025 |
| [rs0x29a/Linux-on-HONOR-MagicBook-14-Pro-2026-AI_ZQC-P_M1010](https://github.com/rs0x29a/Linux-on-HONOR-MagicBook-14-Pro-2026-AI_ZQC-P_M1010) | honor-ec-sensors, чтение tach0x2C/0x2E | ZQC-P, не HLYL; writer не становится доступен от наличия hwmon |
| [Wintego/Honor-PC-Helper](https://github.com/Wintego/Honor-PC-Helper) | Windows WMI и каталоги OEM | Проверен ZQC-P; некоторые коды конфликтуют со старым WmiUtil |
| [HelloZER0/HuaweiWmiControl](https://github.com/HelloZER0/HuaweiWmiControl) | Китайский Windows WMI-клиент | Общий HWMI, HLYL в таблице автора не указан |
| [egormanga/gnome-extension-huawei-wmi](https://github.com/egormanga/gnome-extension-huawei-wmi), [qu1x/huawei-wmi](https://github.com/qu1x/huawei-wmi) | Пользователи sysfs | Не новый транспорт полного EC |
| [aymanbagabas/huawei_ec](https://github.com/aymanbagabas/huawei_ec) | Старый проект EC | Прекращён, MACH-WX92018; адреса не переносить |
| [Maccraft123/it8987-qcom-tool](https://github.com/Maccraft123/it8987-qcom-tool) | IT8987 на Qualcomm | Другой host interface и firmware, совпадение чипа недостаточно |
| [asturking/huawei-fan-controller](https://github.com/asturking/huawei-fan-controller) | Серверный SNMP/iBMC | Отброшен: RH2288H, не ноутбук |

Скачанные исходники закреплены commit SHA в `ec-oem-analysis/sources.json`.
У дополнительных README сохранены Git blob SHA. Новый поиск не отменяет уже
зафиксированные локальные данные18.10 и проверенные native I2EC-команды.

### Публичный HLYL: предметное сравнение

sermart1234/huawei-wmi-tool commit80a6f307ac12f86921a9029289ccf1790c8f0425,
файл `dsdt/HLYL-WXX9_1.8.dsl`. После удаления комментариев/пробелов совпали
тела GTMP, GFNS, GCVA, GFNM, SFNM, GWKM, SWKM, GLIV, SLIV, SBTT, GBCM
с нашим заводским DSDT BIOS1.09. Это совпадение выбранных методов,
не доказательство идентичности BIOS или версии из одного имени файла.
Результат: `ec-oem-analysis/hlyl-dsdt-comparison.json`.

И в публичном HLYL, и в нашем DSDT нет определений методов RDER/WTER/SFND.
Поэтому новые драйверы BRI/FRI/DRA и SFND-инструменты не подключаются к HLYL
простым переносом адресов. WMI GFNM/GWKM уходят в SMI, что само по себе не
подтверждает нужный обработчик внутри SMM; нулевой ответ нельзя считать успехом.

## 5. Путь дампа и границы дальнейшей реализации

1. OEM-снимок уже можно собирать через существующий HWMI: указанные6 температур,
   RPM обоих вентиляторов, GCVA20/30, зарядка/Fn/Turbo с сырыми ответами,
   статусами, временем и указанием источника. Новый драйвер для этого не нужен.
2. В заводском DSDT есть ERAM SystemMemory0xFEDD8300 размер0x100,
   WRAM0xFEDD8200 и CRAM0xFEDD8C00. Это окна конкретной платы, а не разрешение
   читать/писать произвольную физическую память. Не добавлять старый HwOs2Ec
   ради доступа к этим окнам.
3. Для низкоуровневого XRAM наш уже существующий HlylI2EcSession использует
   подписанный PawnIO LpcIO, селекторы11/10/12 через2E/2F, проверяет точные
   плату/BIOS, chipID/canary8987 и SPCTRL1. Это обоснованный имеющийся транспорт
   для диагностического чтения разрешённых RAM-областей; новый SYS не требуется.
4. Сейчас DiagnosticDump не снимает полный XRAM Honor: сообщает пропуск.
   В этой исследовательской работе это не изменено и полный RAM-дамп не выполнен.
   Профиль должен отдельно описать читаемые диапазоны. Регистры оборудования,
   mailbox/FIFO и flash нельзя включать в линейный sweep по всем65536 адресам.
5. Перенос в дамп должен переиспользовать существующую сессию/арбитраж PawnIO,
   явно сохранять маску прочитанных байт, ошибки/время, сравнивать известные поля
   с WMI и исключать параллельное управление. Выбор адреса для чтения использует
   записи в selector I/O-порты; это не setter целевого RAM-регистра, но требует
   той же сериализации. Формат снимка RAM и образ прошивки EC — разные артефакты.
6. Из найденных исходников не следует разрешение новых EC-записей. Предыдущие
   проверенные fan/backlight/SMU пути описаны в README профиля и журналах18.10.

В этом проходе приложение, настройки, служба, драйверы и прошивка не менялись.
На Honor выполнены только перечисленные getter-наблюдения и чтение метаданных;
создана исследовательская папка с копиями OEM-файлов. Сборку/релиз не запускали.
