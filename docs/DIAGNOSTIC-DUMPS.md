# Диагностические пакеты

## VBIOS: время жизни провайдера и lease, 06.11.2026

В сохранённом журнале GM6IR0C 0.1.488 два чтения обычного VBIOS совпали и
прошли проверку; полный EEPROM не подтверждён из-за неизвестной ёмкости.
После `capture_finished`, `info.result` и `command_result` журнал обеих сессий
обрывается после изменения DPI. Стек исключения и WER в архиве отсутствуют.
По этим событиям причина закрытия MagicControl не установлена.

Статическая проверка обнаружила отдельный дефект нашего владения: после
успешного preflight semaphore мог освобождаться до уничтожения временного
NVIDIA provider. Теперь общий `VbiosRomDump` передаёт provider, semaphore и
device reservation одному владельцу: после обычного завершения и после
завершения ещё работающего reader сначала освобождается provider, затем
semaphore и reservation. Нового захвата semaphore внутри teardown нет.

Это исправление границы нашего lease, а не доказанная причина сбоя. NVIDIA
указывает, что [NVML thread-safe](https://docs.nvidia.com/deploy/nvml-api/nvml-api-reference.html),
а [init/shutdown используют счётчик ссылок](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlInitializationAndCleanup.html):
само пересечение корректных вызовов не доказывает неисправность NVML. Если
preflight выбросил исключение до возврата lease, новый порядок этого участка
не покрывает. Реальный повтор на GM6 и причина закрытия приложения пока не
подтверждены; Lenovo 0.1.441 с выключением во время отдельной EC/FAN-пробы
является другим неподтверждённым случаем.

Для следующего журнала Windows при включённом общем логировании добавлены
точечные события `ui.dpi`: начало, завершение `SetWindowPos`, этапы шрифтов и
иконок, завершение каждой страницы и конец обработки. Поля `trace_id`,
`parent_trace_id` и `phase=position/relayout` различают вложенный `WM_SIZE`
и последующую раскладку. События появляются только при `WM_DPICHANGED`, не
опрашивают оборудование и не меняют геометрию. Их отсутствие после последней
метки помогает сузить участок, но не заменяет Windows Event Viewer или WER.
Модель вложенных/отключённых событий проверена offline в Windows и Linux;
реальную смену DPI и вылет эта проверка не воспроизводит.

## Lenovo: общий дамп со штатным Full Speed,02.11.2026

В новом архиве(4)0.1.441 чтение полного SRAM8КиБ завершено до пробы; последняя
стадия — Max EC с записью нашей кривой. Обороты в сохранённых показаниях не
изменились, полного результата/возврата нет, пользователь сообщает о выключении.
PawnIO2.2.0 подтверждён собственным IOCTL. Старый PawnIO, FPT или наличие LLT
не объявляются причиной этого случая. Точная хронология и границы доказательства:
[LENOVO-KWCN](LENOVO-KWCN.md).

По уточнённому поручению владельца восстановлен обычный цикл как у Wujie:
Default → исходная RAM → Max30с → Default → прежний подтверждённый режим.
Весь FAN control идёт через executeFanResource и существующий owner; Lenovo
Maximum включает штатный WMI feature04020000/FNST, не переписывает Manual.
ReadOnly по-прежнему исключает переключения. HPBSD сохраняет прежнюю пассивную
политику; остальные модели не меняются. Более новый FAN epoch запрещает cleanup
или replay старой пробы поверх новой команды.

EC bytes/valid публикуются с flush/atomic replace сразу после чтения, до
производных таблиц/метаданных/Max. ec_snapshot_saved отмечает размер/base/valid;
ошибка сохранения останавливает сбор до Max. Частичная RAM остаётся частичной.
Флаг Full Speed, фактическое изменение RPM и стабильный пик — отдельные evidence.
Повторные аппаратные опыты помощник не запускал; проверка только offline.

## Обновление встроенного Intel FPT16.1,01.11.2026

По поручению владельца16.1.25.1871 заменён на оригинальный FPT16.1.27.2181
из сохранённого официального GIGABYTE G5/G7 VF FB10; Intel PCH CAD обновлён
с1.0.0.94 до1.0.0.105. Третьего Intel runtime нет: прежняя группа IntelFpt
и отдельная IntelFpt19 сохраняются. EXE и reference SYS обновлены вместе,
SHA256 закреплены в collector, CMake и PROVENANCE.json. SYS — пассивная копия
вложенных байтов для проверки подписи; его извлекает/загружает сам FPT только
при явно запрошенном аппаратном чтении.

Побайтно сравнены31запись platform table размеромD0: все пары диапазонов,
включая7A00–7AFF/5180–51FF/5480–54FF, прежние. Единственная разница таблицы —
метка ADL→RPL у записи7A00–7A7F (offset68). Команды приложения остаются
фиксированными `-I`/`-d`; проверка LPC, питания, WNPDriver/SYS и обоих проходов,
Descriptor/region validation не обходится. Все старые даты/версии ниже относятся
к соответствующим исследованиям.

Новая подпись проверена пассивно: Authenticode EXE и SYS —0; для SYS
DRIVER_ACTION_VERIFY с автоматическим выбором и Microsoft secondary —0,
издатель Microsoft Windows Hardware Compatibility Publisher. Intel primary
отдельно возвращает800B0109; это не отказ дополнительной Microsoft-подписи.
Загрузка драйвера, чтение BIOS и исправление выключения на Lenovo этим не
подтверждены. Версия клиентского PawnIO пока неизвестна и к CAD не приравнивается.

`WNPDriver` в этой цепочке — kernel service для Intel PCH Controlled Access
Driver, не PawnIO и не обычный постоянно работающий MEI. Один service/file name
используется разными версиями. По разбору FPT утилита извлекает SYS, создаёт/
запускает службу и удаляет её при cleanup. Поэтому наш отказ «Служба WNPDriver
уже существует; FPT не запускался» — собственный preflight, не ошибка подписи
Windows и не доказательство поломки драйвера. Он срабатывает и на stopped residue;
без пути/версии/хэша/подписи и подтверждения владельца удалять такую службу нельзя.
SCM различает service object и файл драйвера; совпадение имени даёт
[ERROR_SERVICE_EXISTS](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-createservicew).
Есть [сообщение владельца ThinkPad о WNPDriver при Lenovo System Update](https://club.lenovo.com.cn/thread-7872604-1-1.html),
но это другая модель/система и не диагноз клиентского Legion. Новый FPT не
обновляет уже установленный PawnIO и не разрешает удаление чужого WNPDriver.

## Lenovo: отказ от подмены BIOS-ридера при конфликте драйвера,01.11.2026

Пользователь сообщил о выключении Lenovo при попытке MAIN BIOS в22:45МСК.
Последний имеющийся архив `(3)` снят раньше, на0.1.435: LPC8086:7A0C допущен
для FPT16.1.25.1871, но обе попытки остановлены существующей службой WNPDriver;
vendor-процесс не запускался. В0.1.437 (commit49da9435) при таком конфликте был
добавлен переход на Insyde H2OFFT6.76 при наличии H2O ACPI. Это подтверждённое
изменение маршрута, но не доказанная причина аппаратного выключения: нужен
журнал последней попытки с reader_selected/vendor_started/последней фазой.

Автоматический переход после ошибки WNPDriver удалён. Подтверждённый Intel LPC
выбирает FPT, а общий guard прекращает операцию при конфликте службы/SYS,
без удаления или изменения чужого драйвера. Если LPC у KWCN не допущен для FPT,
H2OFFT также не запускается. Общий Insyde путь для Honor/других устройств,
AMI/AMD, VBIOS, EC-дамп, FANs и настройки не меняются. Повторный аппаратный
запуск для проверки не выполнялся. Обход блокировки, downgrade ридера или
замена параметров чтения без разбора нового лога не считаются исправлением.

Повторный файловый разбор присланного `b.zip/b.bin` (SHA256
`304c5bb6fd13f88afcc2a96f3db830991dbd506ed2a527419ade5f3d90b54cf7`)
подтвердил Intel SPI Descriptor:24МиБ, компоненты8+16МиБ,20корректных UEFI FV,
host-read для всех объявленных регионов. BIOS-region —800000–17FFFFF (16МиБ),
ME —1000–3DAFFF, Device Expansion(region9) —3DB000–7FFFFF. Это подтверждение
структуры сохранённого образа, не live-доступа текущей платы. В ZIP только
`b.bin`, в `1111.txt` — обозначения модели/NM-F081/IT5507E/LA2-Q; журнала,
команды, версии ридера или названия программатора нет. `b_IT5507.bin` побайтно
совпадает с двумя извлечёнными участками BIOS; это также не доказывает способ
получения исходного дампа. SMBIOS16МиБ из клиентского лога нельзя выдавать за
полный SPI и нельзя назначать размер24МиБ другому устройству лишь по KWCN.

Наш FPT16.1.25.1871 содержит Intel PCH CAD1.0.0.94. Повторно извлечённый из
сохранённого официального GIGABYTE FB10 FPT16.1.27.2181 содержит CAD1.0.0.105;
EXE SHA25641fa119d68f15dc567df37d9513f433507680a6cdea200ac0cdff9cc88c68aae,
SYS SHA25633abb3e6d36a101c89096d8d30173c79a9efe84f7780fc701fab35e398ac58b2.
Обычная Authenticode Intel действительна; это не проверка загрузки под kernel
policy клиента. В новом EXE тоже присутствует WNPDriver: устранение конфликта
одним обновлением версии не подтверждено. Runtime не заменён.
Источник кандидата: [официальный GIGABYTE FB10](https://download.gigabyte.com/FileList/BIOS/nb-bios-g5_g7-vf-win11-64bit-ddr4-bios-fb10.zip).
Есть [первичный отчёт владельца i7-13700HX/KWCN54WW о полном FPT-дампе](https://github.com/Thomashighbaugh/Lenovo-Legion-Advanced-Bios/issues/46);
он не сообщает версию FPT и не подтверждает работу на клиентском устройстве.

## Стадии в окне дампа EC,01.11.2026

Общий collector публикует `diagnostic_dump/event=stage` через существующий
OperationMessages/INFO binding для Windows и Linux. Показываются подготовка,
драйверы/дисплеи, Default либо ReadOnly, чтение EC с фактическим количеством
прочитанных байт, сохранение таблиц, ACPI/firmware evidence, датчики, доступные
Max-пробы, итоговые сведения, восстановление FANs и сохранение отчёта.
Начало Max показывается только при вызове соответствующей пробы; пропуск Max EC
обозначается отдельно. При повторных попытках EC видны время и доступные байты;
во время ожидания минутного окна — прошедшие секунды с прежней частотой10с.
Это время наблюдения, а не обещание завершить весь пакет за60с.

Стадии видны и при выключенном AllLogs: доставка предшествует фильтру дискового
лога. Сырые EC/телеметрические строки по-прежнему не попадают в окно. Вывод
не добавляет worker, аппаратный опрос, запись EC или второй completion path;
порядок, условия допуска,30с/60с окна, отмена и восстановление не меняются.
Подписи переведены на четыре языка. Проверки transcript — офлайн, без устройств.

## Версия PawnIO без VERSIONINFO,01.11.2026

У SYS из Flydigi в пакете Lenovo `(3)` обычного ресурса версии нет:
`GetFileVersionInfoSize Win32=1813`. Это не отсутствие драйвера и не его версия.
Общий `pawnIoDriverStatus(LoadedDriver)` дополнительно спрашивает уже запущенный
драйвер командой `IOCTL_PIO_VERSION`: без входа, ответ DWORD4байта,
`major<<16 | minor<<8 | patch`. Не запускает службу, не загружает Pawn-модуль,
не обращается к EC/портам/MSR и не запускает BIOS/VBIOS readers. FileMetadataOnly
и `pawnIoInstalled()` остаются пассивными. Отказ/нет прав/старый протокол сохраняют
точную причину; цифры установщика не подставляются вместо неизвестной версии.

Startup, INFO, resource catalogue и `dependencies.json` используют один getter.
В дампе отдельно `file_version` (SYS), `loaded_version`/`loaded_version_raw`
(ответ ядра), их статусы, выбранный `driver_version`/`version_source` и версия
комплектного установщика. INFO показывает цифры на кнопке. Если установщик новее
обеих известных версий (файла и загруженного образа), обновление доступно по
явному нажатию; более новый SYS/загруженный драйвер запрещает downgrade.
Неизвестность обоих источников не разрешает автоматическую замену.

Первичные источники: [официальный ABI2.2.0](https://github.com/namazso/PawnIO/blob/2.2.0/PawnIO/include/pawnio_um.h),
[обработчик версии](https://github.com/namazso/PawnIO/blob/2.2.0/PawnIO/src/driver.cpp).
Этот запрос есть и в2.1.0. `pawnio_version()` из PawnIOLib сообщает ABI самой
DLL (константа2.0.0), поэтому для определения версии драйвера не используется.
Проверка сборки не доказывает фактическую версию Flydigi SYS на чужом Lenovo:
её подтвердит ответ драйвера в новом пользовательском журнале.

## Lenovo KWCN54WW,01.11.2026

INFO использует общий entry/collector и действующие BIOS/VBIOS readers, без
зависимости от preview. Windows EC snapshot — весь проверяемый SRAM window
8КиБ C000–DFFF (EC80000–81FFF), с valid mask, ошибками блоков и реальными адресами.
Это RAM, а не live flash EC. `fan-tables-default.json` содержит исходные13 строк,
native count, descriptor, sensor mask и stages; `lenovo-wmi.json` — фактические
capability/discrete/FAN tables и девять текущих параметров мощности/температур.

Default и Max идут через executeFanResource; общий measureMaximumFans держит
Max30с. Cleanup возвращает Default, затем одноразовый callback восстанавливает
ранее подтверждённый Manual/Max/Min. Новая явная команда, power epoch, смена
WMI-режима или чужое владение запрещают старый replay. Черновики/CPU/батарея не
перезаписываются. Изолированный telemetry helper использует только WMI getters,
без live FAN-controller или I2EC-port sessions. Unsupported/partial не становится
успешным полным дампом. Подробности: [LENOVO-KWCN](LENOVO-KWCN.md).

INFO → «Снять дамп» сохраняет отдельный каталог `logs_<плата>/dumps/dump-UTC-counter/`.
Это диагностический снимок, не файл BIOS/VBIOS для прошивки.

Обычный журнал power после действия/события питания сохраняет серию60измерений
с интервалом1с (раньше10). Это примерно минута наблюдения; медленный getter
может увеличить время. События/ошибки записываются сразу. Частота sensors.jsonl
при дампе и30-секундная проба Max этим изменением не меняются.

## Порядок Default → снимок → диагностика → Max

По запросу владельца от19.10.2026 обычный дамп сначала переводит **вентиляторы**
CPU/GPU в Default через активный typed-контроллер, завершая Manual online.
Батарея, подсветка и лимиты мощности этим действием не сбрасываются.
Подтверждение/ошибки записываются в fans-default-state.json. Затем сохраняются
ec.bin, ec-valid.bin, ec.csv и fan-tables-default.json. Только после этого
запускается SensorRecorder, собираются логи/сведения и выполняются доступные
пробы Max30с. Cleanup каждой пробы возвращает Default. Уточнение владельца
от20.10.2026: после завершения **всего** сбора typed-владелец возвращает свой
подтверждённый режим FANs, который действовал до дампа. Восстанавливается
применённый Manual/online/Max, а не черновик редактора или случайный CSV.
Более новая явная команда/переход питания отменяет старое намерение; неизвестное
или чужое владение не перезаписывается. Возврат проверяется свежим readback,
а не совпадением RPM/PWM. Результат — `fans-restore-state.json`; OSD завершения
публикуется после этой проверки. На неподдерживаемой модели writer не добавляется.
Отмена до Default не пишет; отмена после него до допуска возврата оставляет Default. Если Default
не подтверждён, остаётся пассивная диагностика, обе активные пробы пропускаются.
Отмена до допуска возврата инвалидирует и ожидающий план. Если возврат уже
начал логическую запись, он завершает readback/штатный безопасный cleanup;
закрытие страницы ждёт этот участок, а не обрывает таблицу посередине.
NoName preview/ReadOnly пропускает также сам перевод в Default.

GM6 Default снимает commit/custom/override/boost, не меняя RAM. Исходные RAM-байты
из первого снимка сохраняются отдельно от OEM reference Office/Gaming/Turbo.
Нулевой/неактивный блок не называется заводской кривой. Внутренняя таблица EC
flash пока не экспортируется; OEM reference взят из точного пакета GM6IR0C,
а не получен чтением flash. Фактические флаги первого снимка тоже записываются.
Linux без EC transport сохраняет причину недоступности Default; записей нет.

## Временной ряд

`sensors.jsonl` — одна JSON-запись на строку. Общий сборщик одинаков для моделей
и ОС. Первая запись сохраняется **после исходного EC-снимка**, до Max,
последняя — после возврата. Фазы: `before`, `ec_max`, `ec_restored`, `gpu_snapshot`,
`nvml_max`, `after`. Начальная и конечная фаза записи могут различаться:
чтение нескольких источников не является атомарным.

- `utc`, `elapsed_ms`, `read_duration_ms`: время окончания/длительность чтения.
- `channel=fast`: целевой период 1 с; медленный драйвер может увеличить интервал.
- `channel=optional_providers`: отдельный поток WMI, пауза 5 с между сериями.
- `readings`: device, metric, unit, source, status, value, raw,
  sampled_at_monotonic_ms. Ноль времени означает, что API не предоставил
  отдельную временную метку; использовать окно записи. У NVML field82 метка
  самого провайдера и задержка дополнительно сохраняются в status.
- `value=null`: источник не вернул действительное показание. Это не ноль.
  Диагностический режим не подставляет прошлую температуру батареи/диска.
  Явно помеченный stale SMU/EC не становится свежим значением.

`sensor-summary.json` группирует первое/последнее, минимум/максимум и число
числовых показаний по полному device+metric+unit+source. Это наблюдения только
этого сбора, не паспортные пределы. Минимумы/максимумы стороннего WMI-провайдера
помечены отдельно как его lifetime, не как результат текущей пробы.

## Источники

Windows: общий MonitoringReader (AMD ADL/ADLX, PawnIO Tctl/core energy/package,
Intel Package DTS/RAPL, Windows ProcessorInformation/GetSystemTimes, NPU XRT),
Battery Class IOCTL и независимая подтверждённая EC-температура батареи;
все успешные temperature entries Storage properties 52/51 и отдельные
температуры NVMe SMART composite/sensor1..8, до64 PhysicalDrive.
Ошибки открытия/IOCTL сохраняются с Win32-кодом. Intel I801/SPD5118 сохраняет
температуры подтверждённых хабов0x50–0x57 отдельно от SMBIOS-порядка модулей.
AMD PMLog сохраняет все поддержанные raw sensor IDs, не назначая неизвестным
полям выдуманные единицы. SMU читается только существующим profile/CPUID/PM
guarded reader; setters мощности не вызываются.

Дополнительно читаются ACPI thermal zones (без назначения их CPU), Windows
CPU performance и GPU engine/adapter-memory counters. GPU engine счётчики
относятся к процессу/движку; их нельзя просто суммировать в общий GPU load.
Если уже работают LibreHardwareMonitor/OpenHardwareMonitor, читаются их WMI
Sensor instances с исходными Identifier/Name/типом, включая температуры ядер,
DIMM, VRM, накопителей и GPU. Эти программы/драйверы сборщик не устанавливает.

Linux: все экспортированные числовые каналы hwmon (температуры, границы,
вентиляторы/PWM, мощности, энергии, напряжения, токи, частоты), thermal zones,
power_supply и частоты каждого доступного logical CPU. Имена/полные sysfs-пути
сохраняются. Runtime-suspended GPU сохраняет идентификацию без live hwmon-чтения.
Также работает обычный MonitoringReader с power_supply/RAPL/hwmon и NVML.

GPU: NVML отдельно по UUID, ADLX/amdgpu отдельно по ID для всех AMD-адаптеров,
включая встроенные. NVIDIA memory temperature запрашивается через документированный
NVML field82; отсутствие поддержки остаётся null. AMD сохраняет отдельно board
и ASIC power, hotspot, intake temperature, напряжение при доступности API.
DXGI/DXCore или PCI/sysfs перечисляет все адаптеры, независимо от primary.
Дискретная карта в гибридном режиме не заменяется показаниями встроенной.
Windows GPU counters охватывают также Intel. Отдельный Intel backend использует
официальный IGCL из установленного драйвера: температура GPU/VRAM/VR, частоты,
напряжения, энергия/активность, память и пропускная способность. По двум снимкам
энергии/времени рассчитывается мощность, по global activity/time — загрузка.
Первый снимок не содержит выдуманной производной мощности/загрузки. System/Device
memory locations сохраняются отдельно и не объявляются выделенной VRAM по имени.
Linux Intel использует общий hwmon parser с фильтром физического PCI-устройства,
частоты i915 RPS и xe GT/tile; energy delta/time сохраняется по каждому домену,
а CPU Package RAPL не подменяет мощность GPU. Неэкспортируемые значения остаются
неизвестными. Intel-пробы/управляющие setters не вызываются.

## Остальные файлы

`report.txt` — границы сбора и ошибки; `comment.txt` — комментарий владельца;
`inventory.json`, `battery.json`, `graphics.json`, `nvidia.json`, `radeon.json`, `intel.json` —
инвентаризация и отдельные снимки. `session.jsonl` — копия текущего журнала.
`ec.bin`, `ec-valid.bin`, `ec.csv` — блоки EC и маска прочитанного; после пробы
могут сохраняться `ec-after*.bin`. Непрочитанные нули не являются данными EC.
`settings/` содержит только разрешённые настройки, не произвольные файлы.

Пробы Max30с используют прежние board/API guards и возврат подготовленного
Default. OEM-конфликт не обходится остановкой чужих служб. Лимиты, подсветка,
частоты/напряжения, пороги батареи при сборе не меняются. Во время проб общий
sensor recorder не удерживает hardwareGate и не мешает процедуре возврата.
Сборщик не подбирает неизвестные EC/SMN/MSR адреса ради поиска датчиков.

## NoName

Неизвестный ноутбук автоматически использует NoName. BIOS допускает установку
PawnIO (Windows) и сбор доступной диагностики независимо от наличия EC-протокола.
На неизвестной плате полный EC-доступ/активные пробы не включаются по сходству марки.
Ручной выбор NoName на известном устройстве тоже разрешает обе кнопки для текущего
компьютера; DiagnosticDumpMode::ReadOnly явно отключает EC/NVML fan probes.

## Занятость EC и повторы

Honor HLYL-WXX9-PCB / BIOS1.09 / IT8987: Windows использует существующий
PawnIO LpcIO/I2EC с диагностическим окном0x0000–0x0FFF. Каждый блок16 байт
проверяет ID/canary8987 и доступность I2EC, после чего выходит из PNPCFG
и отпускает общий ISA mutex. Драйвер/модуль не загружается заново для каждого
блока. Файл ec.bin —4096-байтный снимок этого RAM-окна, не образ EC flash;
неизвестные peripheral/SFR/flash области не сканируются. В report.txt отдельно
пишутся проверенные PWM period/feedback getter-адреса. При чтении не вызываются
fan/backlight setters. Активная проба Max для Honor автоматически не добавлена.
OEM-каналы GTMP0/5/7/8/11/14 (CTMP,TSLO,TNTC,CNTC,DNTC,BTMP) записываются
в общий sensor recorder. Неизвестные физические точки не называются DIMM/GPU.
В battery.json также включён свежий OEM fallback GTMP14; сохранённые показания
в диагностическом режиме не подставляются.

Краткая пауза observations возникает также при обычном фоновом чтении состояния
через EcAccess::Transaction. Это не доказательство сна или аппаратной записи.
X5/GM сигнализируют её общим типом EcObservationBusy. Сборщик освобождает доступ,
ждёт20мс с поддержкой отмены и повторяет тот же16-байтный блок, сохраняя уже
прочитанные блоки. Общий бюджет EC —180с. Между16-байтными блоками сборщик явно ждёт2мс,
освободив доступ: простой yield не гарантировал очередь и задерживал мониторинг.
Управление охлаждением продолжает работать. Каждые256 прочитанных байт журнал
содержит progress, время и максимальную длительность блока. Бюджет относится
ко всему медленному чтению, а не только к ожиданию пауз.

Ошибки чтения получают до3 попыток. После постоянной ошибки одного блока
собираются остальные блоки; сбой проверки платы/недоступный транспорт/адрес
останавливают сбор. complete=true возможен только при4096 действительных байтах.
Отмена/исчерпание времени сохраняет частичный снимок и маску. report.txt содержит
число ожиданий, повторов, длительность и адреса окончательно непрочитанных блоков.

В диагностическом режиме батарея ждёт паузу EC до250мс для свежего показания;
обычный опрос интерфейса продолжает пропускать занятый цикл без ожидания.
WMI-провайдерам отведено8с, чтобы первый запуск performance counters не обрывался
через2с. Их отдельный поток не блокирует запись fast-датчиков.
Обычное завершение дожидается текущего прохода WMI, продолжая fast-запись;
незавершённый первый запрос больше не отменяется только потому, что EC уже прочитан.
Явная отмена владельцем/закрытие страницы по-прежнему прерывает провайдеры.

## Источники Intel GPU

- SDK IGCL: https://github.com/intel/drivers.gpu.control-library
  (pinned revision указан в src/third_party/igcl/PROVENANCE.txt).
- API: https://intel.github.io/drivers.gpu.control-library/Control/api.html
- Linux hwmon ABI: https://kernel.org/doc/html/latest/admin-guide/abi-testing.html
- GT/tile: https://www.kernel.org/doc/html/latest/gpu/xe/xe_tile.html

Windows загружает ControlLib.dll только из System32, только при обнаруженном
Intel graphics adapter. Инициализация использует telemetry/Level Zero flag,
не firmware/overclock flags. Все enum/get exports проверяются. Ошибка DLL/API
сохраняет OS identity и конкретный статус в intel.json. Идентификатор Windows
сопоставляется по OS LUID, не по имени или выбору primary. Если соответствие
не подтверждено, сохраняется session identity, а производные счётчиков отключены.
На компьютере без Intel GPU отдельная строка о его телеметрии не выводится.

## WMI и Radeon ADL (0.1.59)

Предел одного WMI-запроса увеличен с512 до65 536 строк. Это предел числа
instances, не датчиков: один instance может содержать несколько метрик.
Каждый диагностический запрос сохраняет provider_records и complete/partial.
При достижении предела, отмене, ошибке перечисления или тайм-ауте уже прочитанные
instances остаются в sensors.jsonl/CSV; provider_status указывает причину.
Ровно65 536 строк без следующей строки считаются полным результатом.
Восьмисекундный бюджет запроса сохраняется, чтобы неисправный провайдер не
задерживал остальные источники неограниченно. Fast timeline работает отдельно.

radeon.json и sensor recorder включают также legacy ADL PMLog из atiadlxx.dll
установленного драйвера (загрузка только System32). Используется общий
AmdTelemetryReader; дополнительный драйвер/ADL SDK runtime не устанавливается.
При отсутствии ADLX сохраняются доступные ADL-показания; при наличии обоих
диагностика сохраняет независимые записи обоих API. Идентификатор ADL включает
adapter index/UDID, PCI bus/device/function и PNP string. Несколько handles одной
карты сохраняются раздельно; имена/порядок primary не используются для склейки.
Тип integrated/discrete берётся из valid mask ASICFamilyType_Get. Если getter
недоступен, тип остаётся unknown, а неизвестный APU не подставляется в dGPU tab.

Сохраняются все supported PMLog IDs с исходными значениями и отдельно основные
GPU temperature/hotspot, usage, clocks, fan RPM, memory activity и power.
Выбор power: Board73 → GFX30 → ASIC23; ID источника указан, все домены остаются
отдельными датчиками. Отсутствующие VRAM size/usage и неизвестные единицы не
вычисляются из других полей. Коды загрузки/ADL/getter сохраняются в api_status.
ADL-путь только читает: управление мощностью остаётся в подтверждённом ADLX API.


## Ограничение WMI и ротация журнала после ревизии

Фиксированные SELECT диагностики/inventory выполняются в отдельном процессе
--diagnostic-wmi, до GUI/EC/single-instance. Передаётся только индекс allowlist,
без произвольного SQL. Каждая строка потоково передаётся по ограниченному binary
каналу; при отмене или общем8с бюджете connect/execute/enumerate завершается
только read-only helper, а полученные строки сохраняются с partial/status.
Windows job с KILL_ON_JOB_CLOSE не оставляет helper при выходе родителя.

Журналы хранятся ровно в трёх парах MagicControl-N.jsonl/battery-N.jsonl,
без сегментации .part. Активная пара удерживается OS file lease до закрытия
потоков; другой GUI/CLI процесс выбирает только свободный слот. Заняты все три —
инициализация возвращает отказ, CLI сообщает причину до выполнения команды.
Выбор/legacy migration сериализованы; lock metadata живёт в .locks и не удаляется.
session.jsonl и battery.jsonl в дампе — snapshot текущей пары, без ротации.
bios-inventory.json читается/публикуется под отдельным межпроцессным lease;
общий файловый writer использует уникальный exclusive temporary и атомарную
публикацию. Ошибка записи сохраняет прежний cache, oversized snapshot отклоняется.
Аномальный power NVML
сохраняет raw и причину отдельно; сомнительное значение не входит в принятые watts.


Max/100% удерживается30с, чтобы прошивка завершила разгон. RPM записываются
в течение всего удержания; стабилизация проверяется в конце, пики сохраняются.
Отмена прерывает ожидание с обязательным возвратом подготовленного Default. Если NVML
не предоставляет вентиляторы/полный безопасный API, отдельная NVML-проба не
запускается. Подтверждённые для платы ограничения доступа остаются обязательными.

## BIOS/VBIOS coordination,01.11.2026

NVIDIA captures may start with an Init-from-ROM `NVGI` header rather than
`55 AA`. The common VBIOS validator follows the documented IFR v1/v2/v3
directory to the PCI chain, preserves the entire source file, and reports the
prefix and IFR version in the manifest. A generic `0000:0000` EFI image is
accepted only after a matched NVIDIA PCI image inside such a container. Identity,
chain bounds and legacy checksum remain mandatory. Source: Linux nova-core
VBIOS layout https://docs.kernel.org/gpu/nova/core/vbios.html . The GM6IR0C
0.1.483 capture contains IFR v3 and a PCI chain at 0x9400; this does not prove
EEPROM capacity, full physical flash coverage, bootability, or explain the
reported display flicker and application exit.

Windows NVML polling and NVFlash share Global\MagicControl.NvflashCapture.v1.
Optional polls skip rather than block while capture owns access; preflight waits
at most8s for a current poll. The initialized NVML provider stays with the pending
capture lease until its reader exits. After each save the size/SHA/exit status
is sealed in <pass>.output.json before post-reader driver checks. The existing
service cleanup/identity guards stay active; phases are explicitly logged.
Pinned driver bytes are compared against signature-verified runtime references
without invoking trust providers again on the same SYS after a ROM read.

If reviewed FPT compatibility exists but its preflight conflicts, only live
Insyde ACPI/vendor evidence allows a switch to the embedded H2OFFT reader.
The unknown/pre-existing WNPDriver service and SYS remain unchanged. Without
compatible Insyde evidence the conflict still refuses capture safely. IHISI
acceptance and returned-image validation remain mandatory. The Lenovo(3) archive
contains an INSYDE UEFI table; its two FPT attempts never started a reader.
These changes address unsafe overlap and preserve partial evidence; an actual
OS/GPU freeze cause and the new physical behavior remain unverified. No reader,
driver load, service cleanup or BIOS/VBIOS operation was executed for validation.

Static package comparison: official target kwcn54ww.exe contains H2OFFT6.63
(x86 EXE), H2OFFT64.sys3.0.0.1 and FlsHook1.0.0.8. Embedded KSCN43WW runtime
contains H2OFFT6.76(x64), SYS3.0.0.3 and FlsHook1.0.0.9. Equal vendor and higher
version do not establish target IHISI compatibility. These target components
were extracted/read as data only; they were not installed, loaded or executed.
