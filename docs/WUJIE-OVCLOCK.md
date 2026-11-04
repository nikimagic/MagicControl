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
