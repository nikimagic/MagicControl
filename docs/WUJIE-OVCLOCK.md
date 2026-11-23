# Wujie FwVars / OvClock — 02.11.2026

Материал: точная SMBIOS-плата WUJIE Series-X5SP4NAG; UEFI read-only,
сохранённый полный снимок BIOS 32 MiB и восстановленный OEM GCUService 5.57.1.2.
UEFI переменные, BIOS/EC, настройки питания и вентиляторы не записывались.

Текущее чтение: `live-20261102-234941.txt`, 180 байт,
`OemMagicVariable`, GUID `9F33F85C-13CA-4FD1-9C4A-96217722C593`, attrs 7.
Project ID 26, APVersion 24, ACpuOverClockSupport 0, MemoryOverClockSupport 0,
MemoryOverClockSwitch 0, OverClockRecoveryFlag 0. CPU freq/current/min/max и
voltage/current/min/max raw 0. Поля CPUPerformanceAndOverClockMenuSupport и
CPUPerformanceAndOverClockMenuSwitch равны FF и не объявляются логическим 1.
UniWillVariable: Win32 203 (переменная окружения не найдена). Снимок записывает
только перечисленные поля; непрофильные сырые данные не выгружаются.
Независимый повтор `core-live-20261103-000943.txt` выполнил тот же вызов
`readOverclockFirmware()` из собранного ядра: читается, Project 26, AP 24,
CPU support 0, memory support 0 и memory switch 0. Текущее чтение не подтверждает
реальную возможность записи, изменения таймингов или физическое действие флага.

Раскладка: восстановленный точный managed `NVRAM_STRUCT`, sequential и default
alignment: memory switch 33h, CPU support 42h, AP version 43h, 32-bit frequency
44h/48h/4Ch, voltage 50h/54h/58h, recovery 5Ch, memory support 60h, menu
support/switch 6Eh/6Fh. На текущей переменной 180 байт префикс доступен,
Project ID совпадает. Это свойства UEFI-структуры, не адреса EC и не показания
реальной частоты/напряжения. Единицы frequency/voltage не установлены.

Сохранённый BIOS `research/wujie-afu-completeness-20261101/bios.bin`, запись
NVAR Data OemMagicVariable file offset 19F1DD7h, entry size BEh, payload +0Ah,
те же перечисленные значения. Это одна историческая запись и не доказательство
её актуальности на момент последующего запуска; текущее чтение независимо.

Полный BIOS: DXE `OemMagicVariableDxe`, GUID 0B2B4F68-3566-40FC-B7E6-FA819993840A,
SHA256 PE 64c001a73ad35f5cf3f60b97530aa52e423ed71f5faa6a84e2b81f555ed8030e;
сравнён побайтно по SHA с ранее извлечённым точным Wujie PE. Статическая
декомпиляция `firmware-dxe/decompiled.c`, функция RVA 0A3Ch. В ветке отсутствия
UEFI-переменной создаётся 180-байтная структура; CPU support=0, APVersion=24,
CPU frequency/voltage и границы=0, MemoryOverClockSupport=0, а
MemoryOverClockSwitch=FF. В существующем пути проверяются APVersion и другой
поле, не обнаружено немедленного чтения 33h или включения разгона памяти.
Ветку SetVariable из кода BIOS здесь не исполняли.

SMM `OemServiceSmm` PE SHA256 eb4fe13277946a1279fb32c94a60daa9b712ffcfD05F6C4C3ED01DB41DF72767,
совпадает с точным сохранённым образом по SHA. Статический поиск прямых ссылок
имени OemMagicVariable дал 5 функций (RVAs 4794,4EB4,5DB0,63C8,7200), результаты
в `firmware-smm/`. Разобранные обращения касаются FAN mode 2Fh, цвета/подсветки
09h..0Dh/0Fh/17h/1Fh/27h; признака чтения 33h/42h/60h в этих функциях не
обнаружено. Это ограничение прямых ссылок, не полный interprocedural анализ BIOS.

OEM 5.57 код проверяет MemoryOverClockSupport==1 и дополнительные условия
прежде чем вызвать SetMemoryOverClockSwitch; CPU OC ограничен capability и
границами. При текущих нулевых флагах эти штатные действия не заявлены доступными.
Проверка значения 1, последствий применения, перезагрузки и отката не проведена.

Из BIOS также извлечён кандидат EC firmware 128 KiB, SHA256
`5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8`;
это update-образ, а не считанная live flash EC. Поиск шаблона 8051
`MOV DPTR,#0751` и адресов 046A/046B/046F обнаружил кандидатные совпадения,
но сам по себе не подтверждает границы инструкций или связь с UEFI-полем 33h.
На сегодня путь UEFI-переменная → EC/контроллер памяти не подтверждён.
Ранее в этом update-образе разбирались EC-политика 751h и ограничения мощности;
они относятся к CPU-бюджетам, а не доказывают реализацию памяти OC. В тогдашнем
readback таблица A38/A39 у работавшего EC указывала на 8325h, а в исследованном
образе — на 6925h: это ещё одно основание не приравнивать update-кандидат к
живой прошивке. Подробнее: `research/wujie-custom-independent-20261029/REPORT.md`.

## Продолжение: отдельные настройки DDR5 и журнал portable, 03.11.2026

Точный BIOS Wujie содержит `CbsSetupDxeSTX`, GUID
`61F0BA73-93A9-419D-BD69-ADE3C5D5217B`, PE SHA256
`0d12578b5dbaf216418925427c2fa8f012df5171a989285b1f72c3d32221d4ad`.
PE из полного 32-MiB снимка и ранее извлечённой ROM2-копии совпадают по SHA256.
IFR varstore `AmdSetup` использует GUID `3a997502-647a-4c82-998e-52ef9486a247`
и размер 1648 байт; отдельное runtime-чтение подтвердило ту же длину. Это не
`OemMagicVariable` и не EC RAM.

| Поле AmdSetup | Формат/варианты из IFR | Текущее чтение |
|---|---|---|
| 0x58, Active Memory Timing Settings | 0xFF Auto, 1 Enabled | 1 |
| 0x59, Memory Target Speed | uint16 LE, MT/s; справка: 2000, 2400, 3200–12000 с шагом 200 | 5600 |
| 0x5B/5E/61/64/67/6A/6D | control tCL/tRCD/tRP/tRAS/tRC/tWR/tRFC1: 0 Auto, 1 Manual | все 0 Auto |
| 0xCC / 0xCD, другая ветвь memory timing/max speed | 0xFF Auto / 0xFFFF Auto; рядом LPDDR SPD Timing | Auto / Auto |

Цифры в соседних raw-полях отдельных таймингов при control=Auto не являются
подтверждёнными действующими таймингами и не используются как они. Значение
5600 — записанная цель, не измерение UMC. Сохранённый SMBIOS inventory от
01.11.2026 показывает два модуля Micron CT16G56C46S5.C8D по 16 ГиБ, номинал и
настроенную скорость 5600 MT/s. AMD указывает DDR5-5600 для Ryzen AI 9 H 365:
https://www.amd.com/zh-cn/products/processors/laptop/ryzen/ai-300-series/amd-ryzen-ai-9-h-365.html
Возможности EXPO самого CPU не подтверждают наличие EXPO-профиля модулей,
поддержку OEM-переключателя или работу выше 5600 на этой плате.

Для CBS-вопроса Target Speed QID 0x41 локальное условие IFR — suppress-if
NOT(QID 0x40 == 1); 0x40 соответствует AmdSetup+0x58. Это локальная зависимость
от опции Enabled, а не доказательство доступности всей страницы BIOS или
срабатывания оперативного разгона. Определения opcodes 0x12/0x17:
https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Uefi/UefiInternalFormRepresentation.h
Следующий вопрос/timing-поля имеют свои условия. Дамп IFR и machine-readable
подтверждения: `research/ovclock-wujie-20261102/wujie-memory-ifr.txt` и
`wujie-cbs-questions.json`. Не интерпретировать приведённый UI input range
как подтверждённые частоты работы установленной памяти.

Другой модуль `Setup` (GUID файла 899407D7-99FE-43D8-9A21-79EC328CAC21,
PE SHA256 49552e0e67a4b98f5cc6a00944122956e4371ebe5e75af1c0f4c98b117b1d608)
объявляет переменную `Setup` GUID `ec87d643-eba4-4bb5-a1e5-3f3e36b20da9`,
229 байт, отдельное поле 0x94 uint16: Auto/4800/5200/5600 MT/s. Условие
grayout этого вопроса ссылается на QID 0x11C SystemAccess; доступность всей
страницы и смысл текущего SystemAccess отдельно не установлены. В сохранённом
NVAR Data по file offset 19F1E95h поле 0x94 равно 5600; это историческая
запись. Запрос `GetFirmwareEnvironmentVariableExW` к этой переменной из
Windows вернул ошибку 203; утверждать её текущее значение или причину скрытия
из этого результата нельзя. Синхронизация Setup+0x94 ↔ AmdSetup+0x59 и связь
этих полей с OemMagicVariable+0x33 остаются недоказанными.

Точный `AmdPbsSetupDxe`, GUID `BBB77CB9-762D-436C-AC40-8EE4901C3446`,
PE SHA256 `c1255c8bd31ad080fc8b77cdc5e9f60e4847cdde7772e1d3fcc8f197d299b09a`,
имеет отдельный `AMD_PBS_SETUP` GUID `a339d746-f678-49b3-9fc7-54ce0f9df226`.
Его IFR содержит условно неактивные выборы VDD_MISC, VDD11, VDD_MISC S5 и
1V8 ALW с кодированными ступенями напряжения. В одной исторической NVAR Data
записи 19CE611h эти четыре кода равны 0. Связь VDD11 с питанием SO-DIMM или
потребителем MemoryOverClockSwitch не установлена, а названия и совпадение
1.1 В не являются измерением напряжения памяти. Это не поля AMD CPU OC из
OemMagicVariable. `wujie-pbs-memory-ifr.txt` — полный отфильтрованный вывод.
Повторное текущее чтение с подтверждением UAC, файл
`research/ovclock-wujie-20261102/pbs-live-retry-20261103.txt`: переменная
доступна, размер 384 байта, attrs 0x7; все четыре поля сейчас равны 0.
В IFR этим кодам соответствуют подписи 0.75 В, 1.1 В, 0.75 В и 1.8 В
соответственно. Это выбранные коды настроек BIOS, а не измеренные напряжения;
принадлежность VDD11 к питанию SO-DIMM всё ещё не подтверждена.

Именно при `MemoryOverClockSupport == 1` штатный Wujie CC 5.57 в одной из
ветвей проверяет ещё `supportGPU`. При `APVersionCheck >= 23` он передаёт в
`SetMemoryOverClockSwitch` поле `CPUPerformanceAndOverClockMenuSwitch`; в другой
версии пути тот же выбор AP>=23 без упомянутого дополнительного условия. На
этой машине AP=24, поле меню raw=0xFF (значение не установлено). Это означает,
что простое объявление support=1 нельзя считать достаточным: статически в этом
пути может последовать передача 0xFF, если другие операции его не заменят.
Динамический запуск, изменение support или переключателя не выполнялись.

В точном восстановленном Wujie GCUService 5.57 обработчик команды
`CPUPerformanceAndOverClockMenuSwitch_ON` задаёт `1` полю
`CPUPerformanceAndOverClockMenuSwitch`, изменяет сохранённый профильный
`MEM.MemoryOverClockSwitch` и вызывает `SetMemoryOverClockSwitch(1)`;
`..._OFF` аналогично задаёт нули. Восстановленный byte-dispatch самого сервиса
содержит присваивания обоим именам. Для каждого вызова общий путь получает и
сериализует структуру и вызывает native `WriteUefi`; это не подтверждённый
непосредственный setter частоты и не доказательство результата контроллера.
Клиентская доступность события и аппаратная реакция 0/1 не проверены.

Есть отдельный служебный обход внутри точного GCUService: при
`HKLM\SOFTWARE\OEM\GamingCenter2\DebugMode == 1` его `LoadProfileAll`
присваивает локальному `NvramVariableInfo.MemoryOverClockSupport` значение 1.
В той же ветке задаются другие отладочные значения мощности и вызывается
`WriteSMAPCTableRegistryAll`; это не эквивалент возможности BIOS, не чистое
включение только памяти и не доказательство изменения контроллера. В 64-битном
представлении текущего реестра `DebugMode` отсутствует, 32-битный ключ не
найден. Реестр проверялся только `OpenSubKey(..., false)`/`GetValue`, без
создания ключей и без запуска OEM-сервиса. Ветку DebugMode не активировали.

Новые прямые cross-reference на OemMagicVariable найдены в сохранённом DXE
GUID `A2DF5376-C2ED-49C0-90FF-8B173B0FD066`, PE SHA256
`49a2c4005eadd6699074e2db89d66e9a5773cd845d8114c4ad6fdd93f4d293c7`.
Его 10 прямых ссылок относятся к пяти функциям; исследованные по данным и
смещениям участки работают с режимом питания, Fn, настройкой графики и
подсветкой. Отдельная функция RVA BF85Ch читает Setup/AmdSetup/AMD_PBS_SETUP:
записываемые ею AmdSetup+0x17B/17C/180..184/186/187 по CBS IFR относятся к
управлению USB-портами; поля памяти +0x58/59 в ней не обнаружены. Показанной
связи OEM+0x33 с AmdSetup+0x58/59 она не доказывает. Аналогичные ветки найдены
в точном PE AMITSE SHA256 `545802494f1a9e5039a7e15f7b384fe618b37736d5f7022dedd368ce184d24ea`.
Это ограниченный анализ, не доказательство отсутствия непрямого пути в BIOS.
В точном `AmdCpmOemSmm` (PE SHA256
`af07b514da4093ffa2a03b0b8f02202a01a1028ba12eb183f0b7f822a0f9e3cf`)
разобранная функция RVA 1668h с именованным доступом к AmdSetup меняет байт
0x53, который в CBS IFR обозначен `DRAM map inversion`; это не 0x58/0x59 и
не подтверждение механизма установки скорости. Функция не исполнялась.

В точном `CbsBasePeiSTX` (GUID `557AFBEF-E4D5-46BA-A47F-30FE957948C5`,
SHA256 `192e5f8804124b7967540f2dae1ae0db218f708567e1496b260f4058fb767af8`)
ранняя функция читает `AmdSetup` и проверяет размер 0x670. Его таблица
связывает байтовое поле 0x58 с ID 0xD02F, а 16-битное поле 0x59 — с ID
0xD030; сериализатор формирует структуру с сигнатурой `$INV` и переносит поле
0x59 из входного буфера. Прямая полная цепочка от прочитанного в конкретной
загрузке AmdSetup до потребителя этих записей пока не прослежена.

В точном `CbsSetupDxeSTX` (PE SHA256
`0d12578b5dbaf216418925427c2fa8f012df5171a989285b1f72c3d32221d4ad`)
функция RVA 0x80A4 проверяет в полученной структуре байт +0x58 на 1 и тогда
передаёт uint16 +0x59 через указатель функции с идентификатором 0xD06DBAFB.
Эти поля соответствуют IFR `Active Memory Timing Settings` и `Memory Target
Speed`. Это подтверждает наличие обработчика настройки в коде BIOS, но не
доказывает, что он исполнился с текущими значениями или что память обучилась на
целевой скорости. Связь с OEM-полем MemoryOverClockSwitch не установлена.
Материалы: `research/ovclock-wujie-20261102/fw-cbs-speed/000080a4.c` и
`cbs-pei-inv-serializer.asm`.

Portable log: версия 0.1.471, сессия `MagicControl-3.jsonl` 00:38:38–00:40:31
МСК; открытие OvClock в 00:39:04, отдельных полей FwVars в журнале нет. Показания
на экране согласуются с предыдущим независимым текущим чтением OEM-переменной.
Более 1100 событий, включая ~261 ACPI exchange, приходятся на первые 10 секунд;
после открытия OvClock за оставшуюся сессию 150 событий. Три записи EC 0x78C
позже в этой сессии помечены `KEYBOARD WAKE`, не являются записью OvClock; это
историческая деятельность приложения, а не действия текущего исследования.
Исследование читало только перечисленные поля UEFI; записи в BIOS и EC не было.

## Поиск фиксации частоты памяти, 03.11.2026

Серия ресурсов 0.1.472: `memory-watch-baseline.jsonl` содержит 20 снимков,
из них 19 показали 2000 MT/s и один 4800; `memory-watch-load.jsonl` содержит
22 снимка по 5600 MT/s при выполнявшемся тесте нагрузки. Оба файла в
`research/ovclock-wujie-20261102/`. Это показания общей частоты двух UMC,
не независимые измерения отдельных модулей; сопоставление слотов DIMM с
каналами не установлено. Регистровый метод и BCLK не проверены внешним
эталоном. Пара снимков не доказывает отсутствие кратких переходов между ними.

В документации Linux AMD Ryzen AI 300 / Strix Point имеет IP MP1 14.0.0:
https://kernel.org/doc/html/v6.16/gpu/amdgpu/amd-hardware-list-info.html
В опубликованной реализации `smu_v14_0_0` таблица содержит
`NumMemPstatesEnabled` и `MemPstateTable[].MemClk`, а проверка доступности
DPM для MCLK/UCLK/FCLK использует `SMU_FEATURE_DPM_FCLK_BIT`:
https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/pm/swsmu/smu14/smu_v14_0_0_ppt.c
Это указывает на общий механизм DPM, но не доказывает, что фиксация FCLK
зафиксирует DRAM или что таблица читается MagicControl на этой машине.

В опубликованном PPSMC интерфейсе 14.0.0 есть отдельные команды ограничения
FCLK и SOCCLK, но явной SetMinUclk/Memclk среди объявленных сообщений нет:
https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v14_0_0_ppsmc.h
В проверенном исходнике Linux функция настройки диапазона принимает FCLK,
SOCCLK и другие домены, а прямые MCLK/UCLK в её switch не перечислены:
https://code.googlesource.com/linux/torvalds/linux/+/0cb552aa97843f24549ce808883494138471c16b/drivers/gpu/drm/amd/pm/swsmu/smu14/smu_v14_0_0_ppt.c
Это ограничение конкретного опубликованного интерфейса и драйвера, не
доказательство отсутствия другого приватного интерфейса в BIOS/SMU. Команд
SMU и записей BIOS/EC в ходе этого исследования не выполняли.
