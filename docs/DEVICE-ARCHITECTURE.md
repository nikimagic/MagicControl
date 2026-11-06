# Профили устройств, протоколы и транспорт

## PawnIO: доступ и диагностика частоты памяти, 06.11.2026

Выбор просмотрщика частоты зависит от проверенного CPU family/model и типа
DDR, а не от имени ноутбука, профиля или preview. Общий `MemoryTelemetry.h`
содержит идентичные для Windows/Linux pure policy и bounded Intel QCLK
decoder; фактический PawnIO collector сейчас есть только в Windows. Если
регистр/CPU/тип памяти не подтверждён, частота остаётся неизвестной. SPD,
SMBIOS configured speed и желаемый режим не подставляются как текущая скорость.
Явный `--no-ec` по-прежнему запрещает чувствительный SMBus/SPD путь; это не
ограничение по модели, а запрошенный пользователем режим без данного I/O.
При неизвестном типе памяти найденный хаб подписывается `SPD5118`, а не
автоматически `DDR5`: один этот тип хаба встречается также на LPDDR5.
https://github.com/torvalds/linux/blob/master/drivers/i2c/i2c-smbus.c

Intel collector сохраняет в диагностике причину отсутствия QCLK, а при
успешных чтениях — исходные `5918` до/после и `5E04`. Неустойчивые значения
сравниваются до трёх раз. Ошибка открытия повторяется через5с; явный
диагностический проход не скрывается ожиданием retry. AMD UMC аналогично
сохраняет причину и исходные состояния/конфигурации обоих каналов. Все новые
raw-строки выдаются только при diagnostic, не в обычный polling-log. Это
облегчает поиск причины, но не доказывает наличие данных или физическую
точность на каждом устройстве.

Архив GM6IR0C от06.11, версия0.1.483: SMBIOS ProcessorId `0x000B0671`
подтверждает family6/modelB7, два type17 — DDR5; модуль IntelMCHBAR.bin
присутствует с ожидаемым SHA256. Эти факты допускают общий Intel reader;
старый лог не фиксировал ошибку/raw QCLK, поэтому причина пустой строки на
этом ноутбуке не установлена. Новый offline тест `memory_telemetry_offline_contract`
проверяет19 профилей включая неизвестный и подставной транспорт под Windows/
Linux. Он не запускает PawnIO и не заменяет проверку реального GM6.

## Intel/AMD DDR4 и дополнительные DDR5, 02.11.2026

Выбор по CPUID и SMBIOS, без списка ноутбуков: Intel family 6 97/9A/B7/BA/BF
с обычной DDR4 или DDR5 использует существующий IntelMCHBAR collector. AMD
19h/74,75 добавлены к DDR5 UMC пути 1Ah/20,24; AMD 17h/60,71 и 19h/21,50
с обычной DDR4 используют только подтверждённый текущим статусом P0 и отдельный
формат low7/3 с номинальным BCLK. Неопознанные модели и LPDDR не получают
чужое преобразование. Оба канала обязаны согласоваться; новые AMD пути и Intel
остаются приблизительными до проверки устройства. Сбой Tctl и повторная пауза
больше не останавливают чтение памяти при доступном модуле и PCI-мьютексе.
Intel 12th: https://edc.intel.com/content/www/cn/zh/design/publications/12th-generation-core-processors-datasheet-volume-2-of-2/system-agent-performance-status-sa-perf-status-0-0-0-mchbar-pcu-offset-5918/
AMD DDR4/DDR5: локальные ZenStates-Core Ddr4Timings/Ddr5Timings и CoreFreq UMC.

Windows DIMM telemetry также допускает DDR4 TSOD через существующие Intel I801
и AMD PIIX4. Читаются только SPD page 0: type byte2 и thermal-present byte14;
соответствующий адрес 18+n проверяется по JC42 capability/config и паре
manufacturer/device, затем читается температура с перестановкой байтов. Нет
смены страницы или записи в датчик, нет привязки адреса к SMBIOS serial.
Не у каждого DDR4 DIMM есть датчик, SMBus или доступная page 0. Linux jc42 не
отождествляется автоматически с DIMM: одно имя драйвера и соседство ee1004
не доказывают принадлежность, а чтение ee1004 может переключить страницу.
Новые Intel/AMD DDR4 пути не проверены физически и требуют сверки на устройстве.

## Intel DDR5 QCLK, 02.11.2026

Windows collector для Intel family 6 models B7/BA/BF и ordinary DDR5 читает
под общим Access_PCI текущий QCLK через IntelMCHBAR 0.2.11: два согласованных
чтения 5918 и поле Gear последнего запроса BIOS 5E04. Скорость DDR вычисляется
из номинального BCLK 100 МГц и BIOS Gear; это приближение, помеченное ≈,
поскольку актуальность Gear и фактический BCLK не подтверждены. Lenovo не
проверен физически. Сырые значения и отказ не превращаются в нулевую частоту;
адреса фиксированы typed API, записи и отдельные GUI-pollers отсутствуют.
Источник Intel: https://edc.intel.com/content/www/kr/ko/design/publications/13th-generation-core-processor-datasheet-volume-2-of-2/system-agent-performance-status-sa-perf-status-0-0-0-mchbar-pcu-offset-5918/

## Текущая ступень DDR5 UMC, 02.11.2026

Windows PawnIO collector читает текущий memory P-state из DBG_MISC 50D6C,
затем CFG_MISC 50200+100*pstate, для двух каналов со stride100000. Поле low16
задаёт номинальныеMHz выбранной ступени; DDR5 data rate равенMHz*2. Статические
SMBIOS/паспортныеSPD скорости не подставляются вместо текущего чтения. SMBIOS
используется только для допуска обычнойDDR5, исключаяLPDDR. Аппаратный допуск:
AMD family1A models20/24, без привязки к названию ноутбука. Другие CPU layouts
не заимствуются. Оба канала должны подтвердить одинаковую частоту, а номер
P-state — остаться тем же до/после чтения; переход повторяется до3раз. Чтение
идёт в существующем poll под Access_PCI, без отдельного worker или setters.

ResourceCatalog получаетfrequency(MHz) иdata_rate(MT/s) с временем чтения.
Общий renderer обеихGUI добавляет скорость перед источником в существующей
строке карточки; размеры/скролл/серийный номер/температура сохраняются. Это
общая частота двухUMC, не вывод соответствияSPD-адреса конкретному каналу.
Номинальная ступень2000/4800/5600MT/s не притворяется измерениемBCLK: HWiNFO
может показывать1996/4791/5590 из-за поправки опорной частоты. Linux reader
и неподтверждённыеCPU пока не поставляют этот показатель. В живом чтении
Wujie подтвержденыP0=2800,P1=2400,P2=1000MHz и изменениеDBG_MISC.low3.
Источники: CoreFreq20de51c amd_reg.h AMD_17_UMC_DEBUG_MISC/AMD_ZEN_UMC_CFG_MISC;
ZenStates-Core Ddr5Timings.ReadRatio. Результат пробы: build/memory-umc-probe-result.txt.

## PawnIO: общая телеметрия устройств, 02.11.2026

Наличие профиля ноутбука не является условием чтения общих датчиков.
В Windows известные и неизвестные устройства с Intel или AMD получают общий
поиск DDR5 SPD5118 через соответствующий PawnIO SMBus backend. Перед чтением
backend проверяет контроллер, берёт общий SMBus mutex и подтверждает тип SPD;
ошибка/отсутствие датчика остаются недоступностью, а не нулём. Intel и обычный
AMD путь ищут адреса 0x50–0x57 без переключения SPD-страницы. Особый HPBSD
MR11 recovery сохраняет прежний точный допуск. На новых устройствах SMBIOS
serial не сопоставляется с адресом SPD без подтверждения. `--no-ec` не открывает
эти PawnIO SMBus пути.

Для AMD семейств 17h–1Ah CPU telemetry также выбирается по CPUID,
а не по имени платы; существующие проверки модуля, декодирования и PCI mutex
сохранены. Intel CPU и PCH сохраняют собственные аппаратные проверки. Linux
использует системный hwmon `spd5118`, также без проверки модели. Это доступ к
наблюдению, не разрешение на запись EC, изменение FAN или лимитов. Физическую
доступность SPD и правильность показаний на каждой плате нужно подтвердить.

## Uniwill identity history, 02.11.2026

`platform_id` (7C3), `rom_id` (770), `project_id` (740) — только история
подтверждённых Uniwill/UWACPIDriver-протоколов. Общий core-предикат
`uniwillIdentityHistoryProfile` проверяет точную пару actual profile/protocol;
он используется владельцем файлов и Windows identity reader. Startup обеих
ОС отсекает остальные устройства до инициализации истории. Honor/Huawei,
Lenovo, MINISFORUM и Unknown не создают даже пустых/unsupported записей.
Наличие EC/драйвера или выбранный preview не разрешает чтение этих адресов.
Ранее накопленные файлы этим исправлением не удаляются и не перезаписываются.
INFO использует тот же допуск до чтения identity, а общая проекция удаляет
чужие ID-строки из старого кэша по собранным SMBIOS-фактам, без GUI-time I/O.

## UWACPIDriver: аргументы — проверять по хэшу,31.10.2026

В пакете10.48.11.331, SYS SHA2566e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235,
трёхаргументный helper1DC0 затирает второй аргумент третьим (включая WIOP).
Это не дефект обычного ECRR/ECRW и не доказательство несовместимости других
версий. Двухаргументный RIOP имеет другой упаковщик; аппаратная адресация
требует самостоятельной проверки. Полный разбор: [UWACPIDRIVER-ARGUMENTS.md](UWACPIDRIVER-ARGUMENTS.md).
Не переносить экспериментальные I/O-пути в общие adapters по одному успеху IOCTL.

## Honor HLYL BIOS read backend, 27.10.2026

01.11.2026: описанный ниже exact-model gate заменён общим Insyde candidate
resolver и динамическим HFDM/PSP validator для Unknown/AMD/Intel. Honor остаётся
проверенной сохранённой fixture; размер16MiB и смещения не перенесены в общий
допуск. Прежние signed runtime/INI/команда-g и общий INFO entry сохранены.
Текущий контракт: [общий Insyde reader](WUJIE-BIOS-DUMP.md).

Общая Windows-дампилка использует Insyde H2OFFT6.76/SYS3.0.0.3 из официального
Lenovo KSCN43WW, SHA сверена с опубликованной Lenovo. Прежняя6.25 удалена;
старый SYS не смешивается с новым EXE. Kernel-mode /kp с каталогом проверен.
Существующие AFU/FPT
и INFO entry point сохраняются. Новый путь требует фактический HLYL-WXX9-PCB,
BIOS1.09, AMD и живую ACPI UEFI OEMTableId `H2O BIOS`; OEM-строка HUAWEI
сама не считается определением семейства. Preview не выдаёт аппаратных прав.
Runtime содержит pinned OEM-reader/driver/CRT и конфигурацию без диалогов,
без образа Lenovo/Honor. Signed FlsHook1.0.0.9 остаётся validation dependency;
FWUpdLcl и старыеVC90 DLL/manifest удалены. API запуска hook не предоставляется.
Два реальных16МиБ чтения6.76 на AMD Honor трассируют только H2OFFT.
Единственная команда `<new output> -g`; смешивание с-s убрано. Base/size
override, flash/reboot switches не предоставляются. Код3010 допускается
только к последующей полной проверке файла (OEM INI silent success mapping).
Private desktop изолирует pre-INI сообщения сторонней утилиты; их текст
сохраняется в events/stderr, запрос WM_CLOSE отклоняет операцию без OK/Yes.
Собственный H2OFFT.log каждого прохода копируется в папку дампа.
Два прохода сохраняются отдельно; дополнительно проверяется известный16МиБ
образ HLYL: HFDM/base/range, PSP и контрольные суммы UEFI FV. SMBIOS0 остаётся
в precheck, expected16MiB объясняется отдельно проверенной картой HLYL1.09.

Два контрольных чтения6.76/SYS3.0.0.3,27.10.2026:16777216байт, одинаковый SHA256
75c813b7fd8667f51f5bf76743723b13f6a0ef7112d6af0522c6071fd46dd445.
Они проверяют путь чтения, а не происхождение/содержимое каждого раздела SPI.

27.10.2026: по уточнению владельца изменения общего host-runtime и
ресэмплирования кривых из0.1.235–236 отменены. FANs возвращены к реализации
0.1.234; требование касается только диапазонов контролов20–100°C /5–100%.
Сохранённые native curves/targets и алгоритмы не заменять.

## HPBSD FANs and RAM dump,26.10.2026

Shared GUI/CLI HpbsdFanController + pure protocols/minisforum/HpbsdFans.h,
exact host/BIOS/CPUID + physical5571 + canonical table admission. Existing
PawnLpcIo is reused; a single typed session centralises latch/ISA/parking.
Only normal CPU/system bases/slopes are writable; no FF handoff, critical-row,
PWM,ROM-ID,platform,SMU,battery or flash writes. Prefix journal covers apply
and Default; foreign tables stay unavailable. NativeFixedFans interface lets
the old Honor editor/worker be reused without changing Honor hardware logic.
HPBSD now exposes all four native buttons. Diagnostic RAM0000–0FFF is complete,
read-only and separate from hardware registers/EC flash. Radeon presentation
keeps the status of a fresh provider when a same-device OS fallback is empty.
Lifecycle/live validation limits are documented in HPBSD README and FAN_MODES.

## MINISFORUM HPBSD,26.10.2026

Отдельный exact SMBIOS профиль minisforum-hpbsd / EliteMini Series, UI monitoring.
Windows native CPU/PawnIO и ITE5571 typed read-only telemetry имеют собственный
BIOS1.06/CPUID19h:75h и live PNP/XRAM/clock guard. SMU PM4C0006–9 чтение подключено
к существующему ApuPowerReader; аппаратно проверены4C0009 и54.4/60/65W.
EC CPU и system fans/temperatures разделены; system sensor не называется GPU.
Нет raw EcAccess, Uniwill-карты, fan/charge/RGB/PL setters или startup restore.
Общий GPU presentation объединяет RDP LUID aliases только по подтверждённому
physicalId; raw evidence сохраняется. Microsoft1414:008C исключён как software.
Linux распознаёт профиль, Windows-транспорт не заимствует. Подробности и
границы аппаратных проверок: src/devices/minisforum/hpbsd/README.md.

## Стандартные действия INFO,25.10.2026

Уточнение владельца: кнопки INFO относятся к фактическому компьютеру, независимо
от известности SMBIOS и выбранного preview. Общая доступность INFO не выдаёт
аппаратных прав профилю из меню. Сбор сведений и Windows PawnIO setup не требуют
известной модели; VBIOS требует реально обнаруженную NVIDIA и PCI/index binding.
Для Unknown/preview/catalogue EC-диагностика только ReadOnly, без FAN-проб.

Windows BIOS теперь выбирает FPT19 по фактическому Intel LPC из уже проверенных
диапазонов, а не только именам HYDROC/YAOSHI/275HX. FPT16 сохраняет прежние
проверенные LPC диапазоны; FPT -I всё равно обязан принять систему. AMD AFU
по-прежнему допускается только при AMI; другой BIOS получает явную причину
отказа в окне/логе. Совместимость не обещается по одному названию ноутбука.
Linux AFU/FPT backend отсутствует и не заменяется запуском Windows-инструмента;
tooltip отражает это ограничение. Геометрия INFO/шапки/скролла не менялась.

## VBIOS NVIDIA, версия0.1.194

VbiosRomDump — отдельный shared collector GUI/CLI, независимый от EC профиля.
Доступ даёт только фактическая NVIDIA OS identity и совпавший NVFlash list;
каталог/preview не выдаёт аппаратных прав. Windows адаптер использует existing
AFU/FPT process runner с отдельной фиксированной NVFlash веткой, protected
RCDATA runtime и driver guard. Linux адаптер запускает отдельный pinned ELF
из root-owned cache. Общие parser/двухпроходная проверка/logging остаются в
platform/common. Новых EC/ROM-ID/FAN/APU writes или restore нет.
[Подробный контракт](VBIOS-DUMP.md).

## BIOS FPT19, версия0.1.191

Общий Windows BIOS collector расширен отдельным VendorTool/embedded group
IntelFpt19; AFU и IntelFpt16 не заменяются. Фактический BoardDescriptor собирается
из registry/SMBIOS и проходит существующий чистый deviceProfile() без EC-проб.
Для HYDROC/YAOSHI дополнительно требуется Intel Ultra9 275HX и допущенный LPC.
Чужой профиль в меню не открывает этот путь. YAOSHI остаётся inventory-only
в отношении EC: ни probes/capabilities, ни транспорт управления не менялись.

FPT19.0.5.1948/driver-reference2508.0.23.0 имеют отдельные SHA pins и защищённый
offline cache. Только -I и -d, без установки чужого OEM-пакета и без команд записи.
Работой собственного временного WNPDriver занимается vendor tool; общий guard
не разрешает ему захватить существующую службу/SYS. Новая проверка SystemTemp
учитывает exact fallback FPT19. Collector не считает region-skip/zero-fill полным
SPI даже при exit0 и двух равных файлах. Linux Windows-tool не исполняет.
[Исследование и границы](../research/fpt19-integration-20261025/REPORT.md).

## KEYs QuickSwitch,24.10.2026

Общий протокол src/protocols/uniwill/QuickSwitch.cpp допущен для пяти exact
профилей: Wujie,GM6IR0C,GM5HG0A,L16,HYDROC. Карта подтверждена отдельно
по OEM MySettingManager каждой модели: Windows768.0/однократный767.0,
Fn74E.4,USB767.4,EC AC fallback726.3. Соседние RGB/USB/FAN биты сохраняются.
Не включать его для Huawei/Honor или inventory-only профилей.
ACRecoveryStatus в BIOS — отдельная OEM ветка: данный EC fallback не пишет
NVRAM и не выдаёт readback EC за доказательство сохранения BIOS/автозапуска.

KEYs использует существующий Gm6Controller для RAM моделей, включая его
модельные driver/identity guards и сериализацию. Общие WindowsDevices не
открывают второй legacy транспорт. Настройки сохраняются в соответствующий
gmSettings(id), восстанавливаются независимым lifecycle заданием при
startup/resume; задача не пишет FAN tables,PL,платформу,ROM-ID или батарею.
После потенциальной записи/strobe автоматических повторов задания нет.
Wujie capture/restore вызывает тот же QuickSwitch, Honor остаётся HWMI/sysfs.

Физический Wujie B7(183) из свежего журнала — тачпад по сообщению владельца;
универсальное толкование MicMute убрано. GM OEM183 сохраняет MicMute.
Уведомление тачпада читает только Windows state, без EC/эмуляции клавиш.

## Офлайн AFU в ресурсах, версия 0.1.186

AMI AFU/SYS подключены как Windows-группа AmiAfu существующего embedded runtime.
Динамический внешний runtime API и зависимость BIOS reader от update library
удалены: подготовка — только проверка/извлечение RCDATA через общий ACL/pinning.
Реальные BIOS/размер/питание проверяются перед /O; EC и его карты не затронуты.
mctl больше не является продуктом или CMake-целью. Диагностика выполняется
в основном EXE без создания MainWindow/EC restore для служебного --dump-bios.

## BIOS AMI / AFU, версия 0.1.185

Общий BIOS facade делегирует реализацию адаптеру ОС. Windows предлагает явный
AFU /O для всех моделей с проверкой реального AMI BIOS. Этот путь не использует
EC-карты или UWACPIDriver. Официальный внешний пакет скачивается по запросу,
проверяется по SHA256 и распаковывается общим ZIP-адаптером в settings/tools.
Общий защищённый runtime cache расширен для проверенных внешних файлов.
EXE/SYS не перераспространяются в ресурсах приложения. Подготовка, чтение,
ошибки и сырой вывод сохраняются в bios-dumps независимо от AllLogs.
Обычные EC-процедуры создают ec-dumps; старые папки не перемещаются.
Legacy MMRD wire 65 сохраняет точный Wujie guard и новым UI не используется.
Linux AFU backend не подключён. Контракт: [BIOS dump](WUJIE-BIOS-DUMP.md).

## Общая непрерывность данных,24.10.2026

DisplayContinuity — уровень ядра для последних подтверждённых UI показаний
всех моделей/ОС. Типизированные кэши не являются новым транспортом, источником
readback или правом записи. Raw control/diagnostic readers не используют их.
Смена физической идентичности/удаление и transient пропуск различаются.
Источник, возраст и retained flags сохраняются: [DATA-CONTINUITY.md](DATA-CONTINUITY.md).

## Общий аудит действий,24.10.2026

Core ActionLog/ControlAudit связывает UI и workers через action_id, а transport
адаптеры сохраняют уже полученные байты/коды. Typed Huawei decorator не меняет
интерфейс или количество аппаратных вызовов. EC, HWMI, I2EC, HID, BLE и sysfs
сохраняют свои допуски. Unknown/preview остаются без нового управления.
Полный контракт: [ACTION-LOGGING.md](ACTION-LOGGING.md).

## Отдельный read-only BIOS ROM2 collector,24.10.2026

INFO делит прежний прямоугольник кнопки дампа на EC/BIOS; геометрия карточки,
inventory и скролла сохранена. EC запускает прежний диагностический сценарий.
BIOS — отдельное явное действие Windows, только реальный X5SP4NAG/N.1.20MRO15,
AMD family1Ah/model24h, точный SHA DSDT и два просмотренных SHA OEM SYS.
Preview/Unknown/другие модели/--no-ec не получают доступ. Linux показывает ту же
кнопку недоступной, без придуманного SPI backend.

Фиксированный wire request0x65 обслуживается существующим isolated worker до
создания FastDiagnosticSource. Он не создаёт EcAccess/FAN/APU controllers,
не загружает OEM DLL, не ставит драйвер и не применяет startup restore.
Единственный DeviceIoControl — MMRD/A494: ROM2 FF000000..FFFFFFFF и три fixed
identity reads по EC82/72/26. Буфер с canary отличает нетронутый ответ от
успешного4-байтного ULONG либо8-байтного zero-extended DWORD: внешний успех
OEM IOCTL недостаточен. Неизменившийся пакет отклоняется как ambiguous.
Raw-ответы передаются parent даже до завершения identity admission:
acpi-responses.jsonl + основной лог bios_acpi/response, с длиной/байтами/причиной.
Выборка ограничена первыми16, identity, началами проходов и любым отказом,
без дополнительных аппаратных вызовов. Исправление0.1.180 не запускалось на железе.
Транзакции по512байт используют общий Global EC mutex и отпускают его между
блоками. Два прохода сохраняются последовательно с прогрессом/отменой.
Watchdog60с между ответами, общий budget6ч, ошибки/отмена оставляют partial,
parent может завершить только getter-child. Обычные firmware requests63/64
по-прежнему не вызывают AML или BIOS reader.

SMBIOS из сохранённого Wujie дампа сообщает ROM32МиБ (Type0 extended0020).
Collector сохраняет фактический runtime SMBIOS размер в admission.json, но
SPI chip capacity остаётся unknown. Успешные16МиБ дважды — complete ROM2 window,
не full SPI. Manifest всегда full_spi_confirmed=false, записи/стирания отсутствуют.
Пользовательский запуск0.1.179 завершился отказом на identity-read до ROM;
оба partial пусты. Помощник аппаратный запуск reader не выполнял.

## HYDROC LPP kernel API,24.10.2026

Общий LiquidCoolingController + pure OEM codec, отдельный Windows BLE/GATT
ILiquidCoolingBackend. Фабрика инертна, сканирование/подключение/управление
только явными вызовами; GUI не добавлен. Новый capability LiquidCooling
допущен только HYDROC. Native EC7C7.1 lease использует прежний typed controller,
свой BIOS/driver/P13/LC guards и masked readback; не меняет FAN-таблицы/PL.
Fixed WMILC_Condition перенесён в bounded helper с отдельным control scope;
обычный dump/INFO его не вызывает. Статус LPP в дампе только cached, без scan.
Linux adapter unavailable. Полный контракт: [LIQUID-COOLING](LIQUID-COOLING.md).

## HYDROC HID firmware evidence,24.10.2026

Explicit diagnostic inventory получает отдельную allowlisted операцию
MagicControlIteLightingEvidence через существующий isolated metadata helper.
Её собственный scope HID_VERSION_QUERY явно отделён от descriptor-only READONLY:
единственная HID команда80H запрашивает firmware, никаких setters цвета/яркости.
Metadata+сырые ответы и ошибки сохраняются как lighting-hid.json. No-ec
передаётся child; старые query indices и остальные модели сохранены. Общий HID
transport/query и OS named mutex переиспользуются runtime и диагностикой.
[Контракт и ограничения](LIGHTING-ZONES.md).

## Diagnostic profile parity and bounded polling logs,22.10.2026

Battery/RGB evidence serializers reuse gmRamFanProfile admission for GM6/GM5/L16;
Honor native context and HYDROC restrictions are unchanged. These serializers use
already captured raw EC bytes and grant no transport or write capability. AMD
DBD1 is explicitly not a restart threshold. Dump settings collection preserves
the original filenames and adds actual-profile settings/power/curve names via
the controller traits; settings-manifest.json records copied/missing/error/limit/
cancelled entries without modifying originals or walking arbitrary files.
Typed polling compares only mode/control/PL/owner state. I/O counters and RPM
are detail payload, not event keys; detail snapshots reuse the existing power
burst and are capped at1Hz. Identical read errors and unchanged native battery
threshold/charger observations are deduplicated. No control/read cadence, OEM
ownership, EC sequence, table, flag or setting behavior is changed.


## L16 control integration22.10.2026

New owner1.zip provides full4096-byte EC: EC Project1A / BIOS74C low6=29/ID6, capability78E.6,
platform6 and native tables. Exact MACHENIKE/L16/N.1.13MAC10 + pinned installed
10.48.11.331 SYS gates the shared typed RAM controller. Per-model AMD limits,
settings/draft/curve files; no legacy X5 startup. FANs/RGB/battery telemetry/upper
and CPU-only controls are connected. Old profiles retain their data/branches.
Read README L16 for physical-validation limits, disabled factory native cycle,
platform6 preservation, DBD lower exclusion and Linux writer boundary.


## MACHENIKE L16,0.1.148

Отдельный exact MACHENIKE/L16/L16, BIOS N.1.13MAC10. Общий каталог обеих ОС,
меню index6, GPU previewAMD7945HX/RTX5070Ti, официальная поддержка catalogue342.
OEM5.60.60.17 ID5_ID6DR_FR содержит тот же embeddedUWACPIDriver10.48.11.331.
ЧтениеEC разрешено только явным INFOдампом: shared reviewed-driver guard и
captureEcBlocks/read-only transport000–FFF, без Default/Max/mailbox/ECRW.
CPU/DIMM использует существующий AMD PawnIO со своими аппаратными проверками.
Архив0.1.118 не содержит EC RAM; ProjectID/capability/RAM tables надо получить
следующим дампом. Защищённые GCUService stubs не считаются восстановленным кодом.
README L16 и BUILD-0.1.148 фиксируют evidence/границы; старые модели не расширены.

## GM5 physical mode button0.1.145

Отдельная кнопка около питания: тот же AcpiTest_EventULong176/FIFO/OSD, что GM6.
GM5 использует свои DefaultFanTable_Office/Gaming/Turbo из OEM MBB5.52.50.0.
49F.1 определяет доступность Turbo; GM6 данные/порядок остаются прежними.
Custom использует собственный CSV/session/draft и AMD FPPT Double codec.
Фабричный mailbox не расширен. Новых GPU/MUX/UEFI/батарейных команд нет.
Подробности и проверочные границы — BUILD-0.1.145.md и FAN_MODES.md.


## GM5 common controls,0.1.144

По прямому поручению владельца GM5 подключён к общим typed RamFan1.5/RGB/native
battery механизмам GM6, со своим exact SMBIOS/BIOS/driver и78E.6 допуском.
GM6Project25 не заимствуется; GM5 требует стабильный non-fillProjectID.
CPU AMD SPL/SPPT/FPPT783/784/785, online AMD PawnIO+NVIDIA; гибридная карточка
следует маршруту Windows780M/4060. Фабричный mailbox/GM6 factory profiles не
расширены. Батарейный7D0=DBD1 не является lower, поле недоступно, upper7B9
и логгер используют существующие общие API. Linux EC writer не добавлен.
Подробности/границы подтверждения: README GM5 и BUILD-0.1.144.md.


## GM5HG0A,22.10.2026

Yilong15Pro добавлен в общий каталог меню обеих ОС (индекс5; остальные сохранены).
Точный профиль допускает в Windows только явный read-only RAM dump0000–0FFF
при BIOS N.1.09MRO19 и проверенном установленном UWACPIDriver11.2.18.347
(SYS e9840521…; точный installed hash из второго архива). В0.1.142 ошибочно
был выбран pin10.48.11.331, из-за чего EC не открывался; исправлено0.1.143. Основание —
собственная DSDT INOU.ECRR/FED50000. Драйверная проверка PnP/path/SHA256 выделена
из HYDROC в общий ReviewedDriverWindows; прежний HYDROC pin и identity сохранены.
FAN/APU/battery/RGB writers и Linux transport не включены: runtime capability
в исходном архиве отсутствует. 7D0 в этой DSDT — DBD1, не доказанный restart.
Подробности и следующий необходимый evidence — README профиля.


Явный Logger EC editor (22.10.2026): карта имён/битов и ширин выбирается по
точному профилю, не по похожей модели. Только Enter запускает существующий
DeviceOperation с identity/OEM/transport guards; нет новых драйверов/авторestore.
Wujie использует EcAccess, GM6/HYDROC — отдельный explicit debug API в имеющемся
NativeBatteryController; thresholds-only HYDROC не расширяется. Honor и Linux
без raw backend остаются недоступными. Подробности и read-only адреса — BATTERY.md.

Уточнение UI батареи20.10.2026: единый Wujie набор100%/95%/85% для всех
моделей и preview обеих ОС. NativeBatteryController0/1/2 не является
процентным backend и не связан с этими кнопками по индексу native mode.
GM6 и HYDROC получают по явному поручению отдельный проверочный Windows API
100/95/85 с restart80 (BATTERY.md), общий механизм проверки/отката при
независимо восстановленных картах/guards. Конфигурация не объявляется
физической отсечкой. HYDROC thresholds-only transport разрешает только7B9/7D0,
native7A6 и read-only diagnostics остаются отдельными. Linux writer не включён.


## Исправления подсветки20.10.2026

Общий KeyboardBacklight разделяет selected brightness и native power-disable:
explicit Off по-прежнему level0+bit1, temporary blank может владеть только78C.1.
Wujie adapter flash больше не сохраняет effective blank как выбранный Off,
возвращает лишь собственный эффект при совпавшем live ownership и сохраняет Fn.
GM6 fixed/custom/Cycle используют один exact native converter; raw INI не
перекодируется. GM6 lifecycle worker восстанавливает только явно сохранённые
Keyboard* при startup/подтверждённом resume, без FAN/APU и cycle autoplay.

KeyboardLightingPause и power epoch согласуют existing Cycle6 с restore/flash/
сном. Цикл ждёт свежий admitted final state, при uncertain/foreign change
останавливается. Defer разрешён только pre-write transient retry. Периодические
шаги остаются Background DeviceOperation и не мигают общей доступностью кнопок.
RGB journal сообщает caller результат rollback, не только log; RGB apply group
не защёлкивается поверх чужого канала. Read-only SCM/process preflight блокирует
active/unknown OEM, но не считается эксклюзивной арендой против чужого драйвера.

Honor BIOS1.09 имеет отдельный native idle/fade classifier, selected/actualPWM,
known retained-HIGH resume baseline и checked PNPCFG finish до success.
Фиксированный read-only keyboard context снимается в parent; диапазон RAM0..FFF
не выдаётся за полный PWM context. GKBT/SKBT и существующий Linux sysfs timeout
делят один bounded readback/owned rollback protocol. Новый SFR sweep не добавлен.
Дамп сохраняет keyboard-initial.json и keyboard-after.json; unavailable/error
не заменяются числом0. Карточки, геометрия, палитра UI и скроллбар не меняются.

## Firmware evidence в обычном дампе (20.10.2026)

После исходного EC-снимка кнопка «Снять дамп» сохраняет `battery-policy-initial.json`
и `firmware-before/`: доступные ACPI/SMBIOS и reviewed UEFI banks. После минутного
окна телеметрии `firmware-after/` повторяет только reviewed UEFI и, в Windows,
read-only состояния фиксированных OEM-служб/процессов. Это OS evidence,
не образ исполняемой BIOS/EC flash; пороги и прошивка этим сбором не меняются.

Общий `FirmwareEvidence.h`, fixed wire requests0x63/0x64 и сериализация дампа
переиспользуют существующий isolated diagnostic worker. Firmware dispatch идёт
до создания FastDiagnosticSource: GPU/PawnIO/EC/controllers для него не открываются.
Parent deadline30с на проход, child loop budget20с; timeout/cancel завершают
только read-only child. Принятые артефакты, SHA256, `records.jsonl` и manifest
сохраняются и при partial. `capture_finished` означает завершение прохода;
успех/ошибки и проверки каждого источника остаются отдельными полями.
Отсутствие перечисленных OEM process names относится только к этим именам;
снимок SCM/Toolhelp не гарантирует эксклюзивное владение EC.

Windows: ACPI Enum/GetSystemFirmwareTables, RSMB, bounded registry cache
`HKLM\HARDWARE\ACPI`. API возвращает лишь первую таблицу одинаковой signature:
повторная enumeration occurrence отмечается явно, независимые registry instances
сохраняются отдельно. Bad raw checksum/length сохраняется с validation, а не
становится валидной AML. Linux: sysfs static/dynamic ACPI и два raw SMBIOS-файла;
это не Windows RSMB wrapper. Native sources открываются read-only; MSDM/SLIC
не включаются. Не выполняются AML methods, SPI/physical-memory reads или OEM DLL.

Три независимые UEFI reads — OemMagicVariable, UniWillVariable, OemMagicDoor,
GUID9f33f85c-13ca-4fd1-9c4a-96217722c593 — разрешены только для настоящих exact
GM6/HYDROC, независимо от preview. Unknown и другие модели получают ACPI/SMBIOS
и явный not_applicable OEM banks. Windows временно включает privilege лишь
child-процесса и восстанавливает её; Linux не монтирует efivarfs и не меняет права.
Linux attributes4байта отделяются от payload; actual lengths сохраняются без
padding.188/512 из OEM-кода — reference prefix/buffer, не обязательный размер.
Offsets0x30/31/32/43 двух основных banks — raw candidates, не applied battery
policy; к Door они не применяются. Ошибки прав/legacy/отсутствия остаются данными.

Лимиты:16МиБ на артефакт,256МиБ payload на проход,8192 records; UEFI до64КиБ.
Manifest и journal имеют отдельные budgets. Выходные имена генерируются в трёх
фиксированных каталогах, исходные registry/sysfs paths остаются metadata.
Ни новый BIOS updater, ни setter процента этим механизмом не разрешаются.

## GM6IR0C (18.10.2026)

Отдельный профиль и typed Windows Gm6Controller в devices/mechrevo/gm6ir0c,
карта в protocols/uniwill/gm6ir0c. OEM-код и таблицы из пользовательского архива;
Журнал0.1.59 подтвердил EC readback/пробу Max с возвратом; исполнение CPU PL
остаётся отдельно непроверенным. Не передаётся legacy
X5-обработчикам через createDefaultBackend. Нет startup restore FANs/APU и записей при
выходе. FANs/SETs делят один gate. SETs CPU-only `setCpuPreset` свежо читает OEM
PL defaults и меняет только783/784/785; complex whole-system mode751 остаётся
наблюдением, не командой из SETs. Отсутствующие defaults не заменяются архивными
ваттами. PL readback не доказывает MSR/RAPL enforcement. Подробности — README профиля.
Linux распознаёт профиль, но не получает EC transport или право Windows-записей.
Батарея GM6/HYDROC: общий NativeBatteryController и typed BatteryProtectionMode, маска7A6.4–5, именованные
профили0/1/2. Не выдавать за процентный API X5/Huawei. Только явно сохранённый
BatteryProtection восстанавливается после запуска/сна; FANs/APU не затрагиваются.
Restore имеет до пяти pre-write transient попыток в окне30с, без перехвата OEM;
identity/pin/INI/unknown mode и uncertain write не ретраятся. Результат rollback,
включая unknown/ownership lost, возвращается caller, не скрывается только в log.

RGB SingleZone19.10.2026: GM6 capability Backlight включена; runtime RGB0x766.2
проверяется общим KeyboardBacklight. Core выполняет masked setters/readback/
ограниченный rollback, Gm6Controller добавляет точную identity/OEM/gate проверку.
Wujie UI и ConfigurationRestore используют то же ядро, модели задают калибровку.
Программный KeyboardColorCycle также общий:6UI RGB цветов, интервал1–3600с,
ожидание DeviceOperation, проверка expected RGB/effect, остановка при чужом
изменении. NoName/preview не создают его writer. На выходе остаётся последний
статический цвет; startup-autoplay отсутствует. Params — keyboard-cycle.ini,
подтверждённые GM6 параметры — отдельный gm6ir0c-device-settings.ini.

## Владение кодом

Журналы обеих ОС используют `defaultLogDirectory()` из общего Logging:
`logs_<BaseBoardProduct>`, затем реальный `SystemProductName`, затем
`logs_Unknown`, если SMBIOS пуст или содержит заглушки. Это правило действует
также для NoName: неизвестный профиль не удаляет фактическое имя платы.
Предпросмотр/выбор модели в меню не меняет каталог. Пробелы, управляющие и
недопустимые для имени файла символы заменяются на `_`; Unicode сохраняется.
Дампы идут в `dumps` внутри текущей папки журнала, а при выключенном журнале —
в ту же модельную папку по умолчанию. Старые `logs` не перемещаются и не удаляются.

- `src/devices/DeviceRegistry.cpp`: выбор профиля по точным manufacturer/baseBoard SMBIOS.
- `src/devices/mechrevo/<плата>/Profile.cpp`: идентификация, отдельные возможности FANs/APU/батареи/подсветки/SMU и привязки транспорта.
- `src/devices/mechrevo/x5sp4nag/windows/`: FANs, зарядка и восстановление нашей платы; SMU-читатель теперь в platform/windows/pawnio с отдельными schema guards.
- `src/devices/huawei/hlyl-wxx9/`: профиль и typed-контроллер Huawei; источники и границы в его README.md.
- `src/protocols/huawei/Hwmi.h`: HWMI-команды, не регистры EC.
- `src/protocols/huawei/BatteryThresholds.h`: общая Win/Linux семантика пары,
  включая stable native disabled0/0, bounded readback и собственные rollback values.
- `src/platform/windows/huawei-wmi/`: OemWMIMethod Windows, без стороннего драйвера.
- `src/platform/linux/huawei-wmi/`: штатные sysfs пороги зарядки и Fn Lock.
- `src/protocols/uniwill/x5sp4nag/`: проверенная карта регистров, семантика мощности и чтение вентиляторов. Карта не является универсальной для Uniwill.
- `src/platform/windows/uniwill-acpi/`: сырые IOCTL, сериализация и дескриптор драйвера; не выбирает модель и не содержит таблиц FANs.
- `src/platform/windows/pawnio/`: доступ к мониторингу через PawnIO.
- `src/platform/windows/BackendFactoryWindows.cpp`: выбор EC-транспорта по профилю.
- `src/platform/linux/`: системные источники Linux. Подтверждённого EC-транспорта пока нет.
- `src/gui/`, `src/core/`: общий UI, конфигурация, состояния, сериализация и проверка доступа.

Уточнения статической ревизии19.10: Wujie — внешний эталон всех моделей;
новый аппаратный профиль не создаёт собственный renderer/карточки/скролл.
HWMI mutex Honor находится в Global Windows namespace, но не даёт эксклюзивного
владения против PC Manager. `LaptopControl::readTelemetry()` Honor содержит
только typed HWMI getters, без I2EC/backlight/PNPCFG. Полный `read()` отдельно
добавляет backlight; такой портовый путь и active writers нельзя прерывать
как изолируемый системный getter. Linux default telemetry использует sysfs.

Общий RAM Fan1.5 factory export расположен в `protocols/uniwill/RamFanFactory`,
exact adapters — GM6 controller и `platform/windows/RamFanFactoryWindows` HYDROC.
Он разрешён только явной диагностике после Default/initial RAM snapshot, требует
identity/owner/default/LC guards и не пишет table payloadF00–F5C. APP cleanup
маскированный и только свой; ACK/двойной raw snapshot/validity разделены.
Factory-only allowlist не расширяет general FANs/Max/RGB capabilities HYDROC.
Все эти уточнения описывают исходники; новая сборка/работа железа не утверждается.

Существующие публичные заголовки Windows-контроллеров сохраняют API для GUI.
`MagicControl/EcRegister.h` — совместимый include, ведущий к карте X5SP4NAG.
Имена `mc::windows` сохранены для существующих Windows-контроллеров.
Сырые EC-операции и общий gate не означают разрешения применять карту нашей платы к другой.

## Правила допуска

1. Идентификация модели не равна разрешению управления. `BoardDescriptor::known()` означает только наличие записи в каталоге.
2. 11 плат из 14 пользовательских отчётов зарегистрированы с выключенными возможностями управления и без identity probes/протокола/транспорта.
3. X5SP4NAG имеет подтверждённый raw EC профиль: точная SMBIOS-плата, транспорт `windows-acpi-driver`, затем проверка EC 82/72/26. Неизвестная плата отклоняется до чтения этих адресов.
4. Неизвестный профиль не открывает OEM EC через фабрику. Общий мониторинг ОС работает независимо от EC.
5. Присутствие драйвера, совпадение ProjectID, CPU/GPU или торгового имени не расширяет допуск.
6. Возможности профиля описывают подтверждённую реализацию. Рабочая доступность дополнительно требует нужной ОС, транспорта, прав и успешной проверки identity.
7. Входы управления FANs/APU/зарядкой вызывают `ensureCapability(feature, protocolId)`:
   проверяются и конкретная возможность, и соответствие протокола. Телеметрия SMU
   и доступность подсветки учитывают `supports(feature)`. Добавление другого протокола
   не разрешает применять к нему старые обработчики X5SP4NAG.
8. HLYL не получает raw EC transport/boardVerified. Его LaptopControl определяет
   возможности по ответам OEM getter и проверяет readback каждой записи. Создание
   контроллера и первый опрос не применяют сохранённые параметры.

## Добавление ASUS, Lenovo или другого устройства

Создать `src/devices/<производитель>/<плата>/Profile.cpp` и зарегистрировать профиль.
Для неизвестных функций оставить возможности выключенными. Если команды отличаются,
добавить протокол в `src/protocols/<семейство>/`; если меняется способ доступа —
реализацию в `src/platform/<ОС>/<транспорт>/` и её выбор в фабрике.
Одна модель может использовать несколько транспортов: EC через OEM ACPI, SMU через
PawnIO, батарея через системный API. Не копировать интерфейс и весь OS-слой на каждую модель.
Пустые папки и фиктивные рабочие драйверы для ASUS/Lenovo заранее не создавать.

Источники и дампы: `research/laptop-reports/` и `research/devices/<плата>/`.
Исходные пользовательские отчёты не входят в `GitHub_Releases`.

## Проверка переноса

Сборка GUI/CLI для Windows и Linux выполняется отдельно. Аппаратные режимы,
таблицы, маски и алгоритмы управления при переносе файлов не меняются.
Сборка не подтверждает работу на новых моделях. Тесты и аппаратные пробы без
отдельного запроса пользователя не запускаются.

## NoName / discrete GPU (19.10.2026)

NoName в DeviceHeader — UI fallback, без EC identity/transport. Hardware
фабрики всегда используют реальный SMBIOS. GPU имеют собственные type/vendor/ID
guards: ADLX UniqueId+device/subsystem или Linux PCI/unique_id. Radeon setters
требуют подтверждённый discrete, текущий диапазон и readback. Встроенный Radeon
не подставляется в dGPU-карточку, но сохраняется в дампе. Гибридный primary
адаптер не заменяет список остальных GPU.

## Intel GPU telemetry

IntelGpu — read-only vendor adapter, независимый от Intel CPU/PawnIO и EC-профиля.
Windows: IGCL с текущим Intel display driver. Linux: те же общие hwmon чтения
плюс i915/xe частоты. Общая схема GpuReading/SensorReading и тот же dump recorder.
IGCL flags/exports для firmware/tuning/setters не загружаются. Наличие CPU Intel
не означает наличия Intel GPU; report/intel.json зависят от vendor0x8086 в списке
графических устройств. Аппаратная поддержка отдельных sensors определяется API.

## Раздельные состояния возможностей после ревизии19.10.2026

DeviceCapabilities описывает реализованные функции точного профиля. Возможности
Honor FANs/PowerLimits/Backlight включены в каталоге, но raw EC backend/identity
остаются пустыми: они доступны только typed HWMI/I2EC/SMU с собственными guards.
Runtime writable определяется ОС, getter, BIOS, правами и текущим владельцем.
GM6IR0C отдельно сообщает commandReadbackConfirmed/fanResponseConfirmed; ни один
из этих флагов не означает измеренное исполнение CPU PL. Проба Max с возвратом
подтверждена новым пользовательским архивом; общая надпись больше не отрицает
успешный readback текущей сессии.

## Дополнительная телеметрия19.10.2026

PCH читает отдельный typed IntelPchReader через подписанный IntelPCHThermal0.2.11
(identity/read_raw, PCI allowlist внутри модуля, hash pinned). Его температура
не подменяет CPU/SoC/батарею. NVIDIA NVAPI является Windows supplement общего
NvidiaGpu, с PCI bus/device/subsystem identity и отдельными raw/status каналами.
GetThermalSensors — необязательный undocumented getter; смысл каналов задаётся
поколением GPU, неизвестные каналы остаются именованными индексами в дампе.

Windows PnP MostRecentPowerState, NVML P-state, primary display adapter и OEM
GPU policy0x78B — отдельные наблюдения. Ни одно не даёт права переключать MUX.
GPU query может разбудить dGPU: PnP state пишется до и после основного опроса,
нет искусственного renderer/workload. Документация и границы —
research/devices/gm6ir0c/thermal-sources-20261019.txt.

## Eluktronics HYDROC-16 G2,19.10.2026

Отдельный профиль exact SMBIOS, только batteryCharge capability. Общий
NativeBatteryController использует точные guards платы/драйвера и узкий backend
windows-acpi-battery-policy (запись только7A6.4–5). Generic EcAccess/legacy X5
и FANs/PL/подсветка этим не включаются. Typed diagnostic
session использует общий UWACPI transport в режиме read-only, exact PnP/service
и pinned SHA драйвера. Raw EC0x000–0xFFF и OEM protocol candidates собираются
общим дампером; Default/Max принудительно выключены и в shared API.
См. src/devices/eluktronics/hydroc-16-g2/README.md.

Каждый дамп дополнительно сохраняет `dependencies.json`: PawnIO SCM до/после
сбора и наличие файлов runtime. Единственный SCM getter находится в Windows
platform и используется также startup/установщиком; он не запускает службу.
Список runtime генерируется из `cmake/PawnIORuntime.cmake`, без второго списка
модулей. Ошибка проверки остаётся unknown/null. Linux явно not applicable.
Регистрация драйвера, работа службы, наличие модулей и результат чтения датчика
являются отдельными фактами; последний сохраняется в sensor/session logs.

Минимальное окно дампа: обе ОС и оба режима собирают датчики не менее60секунд
от первого сохранённого fast-замера после EC. Время inventory и Max входит в
это окно; если они закончились раньше, recorder продолжает пассивный сбор.
Отмена прекращает ожидание оставшегося времени. sampling-window.json содержит
целевую длительность, фактический span, число fast-записей и minimum_met/cancelled.
Фатальный отказ recorder не маскируется как выполненная минута.
Это отдельно от Power burst60образцов и от Max-пробы30секунд.

SETs battery: отсутствие EC-контроллера не блокирует MonitoringReader и системную
карточку батареи. Windows/Linux показывают реальные питание/заряд/температуру,
циклы и здоровье, если источник их предоставляет, включая HYDROC и Unknown.
Без charge backend ни один процентный пресет не подсвечивается как применённый;
setter/допуски Wujie и Huawei остаются отдельными от системной телеметрии.

Поставка OEM-пакетов19.10.2026: PACKAGES.json в runtime/Uniwill и общий
cmake/UniwillRuntime.cmake контролируют комплектность/неизменность SYS/INF/CAT.
Каждая модель выбирает только проверенный собственный transport/guards; наличие
двух пакетов в Windows portable/EXE не меняет драйвер ОС и не расширяет EC
capabilities. Геометрия/renderer карточек Wujie этим не затрагиваются.

## Power-input diagnostic series,20.10.2026

Общий PowerInputDumpSession/PowerInputDump.cpp оркестрирует3read-only обычных
дампа actualGM6/HYDROC; UI-адаптерыWin/GTK передают только acknowledgements.
DiagnosticPromptChannel общий cancellable rendezvous, не hardware gate.
Стандартная кнопка/карточки остаются прежними. OutputRoot добавлен последним
optionalаргументомcreateDiagnosticDump; обычный CLI/GUI по умолчанию не меняется.
Состояния проверяются по свежим OS Battery Class/sysfs данным, а не по bit7CC,
который исследуется. Decoder/enable/charge writes из эксперимента не добавлены.
Схемы session/preflight/validation и ограничения — BATTERY.md.

## Отдельные GPU карточки и display route,20.10.2026

По уточнению владельца выбирается GPU, которая выводит изображение на монитор
с окномFANs; busiest/render selection пока не реализуется. Компонентыданных
IntelCard/NvidiaCard/AmdCard находятсяотдельновgui/common/gpu; CardLoader
использует тотжеProcessorCardModel/общийWujie renderer и geometry/fonts.
CPUIntelмалыйблокPCH/Package/Intel·PawnIO, TjMaxсохранёнвдиагностике.

MonitoringSnapshot.gpuCards отдельно содержит per-adapterпоказания/routes/identity.
WindowsGDI monitorname наUI сопоставляетсясQueryDisplayConfig sourceLUID,
LinuxGDK connector наUI — сconnected+enabled DRM PCI. По именимоделиадаптеры
необъединяются; неизвестныймаршрут невыбираетNvidiaпоумолчанию. Пометка«гибрид»
видимавначалезаголовкаиозначаетobservedOSiGPU+dGPUavailability, анепроверенный
firmwareMUXmode. РежиммаршрутаинагрузкадвухGPUразныепонятия.

collectGpuPresentationтретьимаргументомMonitoringReaderdefault=false;
explicittrueтолькоGUIFANs. Контроллеры/FastDiagnostic не делаютновыхтяжёлых
metadata/IGCLpolls. Physicalprocessors[1], thermalgraph, ECfanRPM/PWM остаются
прежнимдискретным/общимохлаждением;новаякарточкаIntelнеуправляетэтимисточником.
GPU-onlyIntelpowerпоканеподтверждён: невыводитьCPUPackage/RAPLкакiGPUВт.
Точнаяпривязкапамяти/providerсохраняетunknown вместоVRAMдругойкарты.
План/primaryAPIdocs/ограничения: research/analysis-20261020/hybrid-active-gpu/PLAN.txt.
