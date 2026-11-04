# Универсальное чтение BIOS/SPI: GitHub и первичные источники

25.10.2026. Прочитан41 файл исходников/документации, сохранены снимки GitHub.
Для31 файла Git blob SHA1 совпал с деревом репозитория; несовпадений нет.
Ни инструменты, ни драйверы, ни аппаратные команды, ни тесты проектов не
запускались. Приложение/portable/EC/BIOS не менялись и не пересобирались.

## Практический результат

Для Intel/AMD x86-ноутбуков наиболее общий программный принцип — читать
сам flash через контроллер SPI либо проверенное ROM-окно. Содержимое может
быть AMI, Insyde, Phoenix или другим: алгоритму чтения байтов марка UEFI
не определяет транспорт. Но PCI-ID/поколение контроллера, права чтения,
карта адресов, ОС и доступный драйвер определяют, что реально можно прочитать.

**Готового средства «любой BIOS, целиком, из обычной Windows, без изменения
защиты» среди проверенных проектов не найдено.** Это не доказательство
невозможности любого будущего инструмента; это результат проверки конкретных
реальных реализаций. Ближе всего к общей кодовой базе — flashrom/flashprog.
Их обычный Windows internal backend отсутствует. Для MagicControl практично
одно ядро сбора с несколькими транспортами и одной проверкой полноты.

Для обычных SPI NOR-чипов самый широкий охват даёт внешнее физическое чтение
совместимым программатором. Тогда vendor UEFI и host permissions PCH не
определяют доступ к самим выводам flash. Это отдельный физический способ,
не обещание программной кнопки без разборки. Нужны соответствие чипу/интерфейсу,
учёт нескольких flash-компонентов и корректная электрическая изоляция.
Даже это не означает поддержку любой NAND/eMMC/SoC/OTP-конфигурации.

## Сравнение проверенных вариантов

|Проект/механизм|Что реально даёт|Главная граница для нашего приложения|
|---|---|---|
|flashrom|Общая C-реализация, Intel/AMD chipsets, различные chip buses и внешние программаторы|Windows не имеет internal programmer; наличие модели в таблице не доказывает фактического допуска/чтения|
|flashprog|Общая C-реализация плюс полезный AMD ROM3 reader|Windows PCI/internal исключён; обычная инициализация контроллеров может менять write-enable/clock settings|
|AMD flashrom-amd|Первичный vendor-код SPI AMD и распознавания FCH revisions|Также не предоставляет Windows internal backend; нельзя объявлять применимость к любому AMD по одному VID|
|CHIPSEC2|Windows/Linux/UEFI framework; Intel SPI hardware sequencing, region/permission diagnostics|Прочитанная команда spi использует Intel HAL; Windows helper требует отдельного допуска; dump имеет существенные допущения о размере/ошибках|
|rflasher|Общее Rust/no_std ядро SPI NOR, Intel/AMD controller abstractions и внешние backends|Alpha по README; готовый internal Linux-only; Intel constructor пытается enable BIOS writes|
|Google Pawn|Небольшая C++ реализация Intel SPI hardware sequencing|Не PawnIO; Linux/devmem, старые Intel generations, фиксированные16МиБ|
|Linux SPI-NOR/MTD|Драйвер ОС может предоставить read-only device и размер/partitions|Только если подходящий driver/device реально доступен; аппаратный read protection остаётся|
|UEFI SPI NOR protocol|Стандартные FlashSize/GetFlashId/ReadData при наличии protocol instance|Нет гарантии экспорта в конкретном OEM BIOS; FVB/volume dump отдельно не означает весь SPI|
|Intel FPT|Чтение физического Intel SPI независимо от марки UEFI при совместимой ветви и разрешениях|Intel-only, версии по семействам, драйвер/permissions; нашу1871 привязали к официальному OEM источнику|
|AMI AFU/O|Чтение через firmware interface; у нашего Wujie два равных32МиБ образа|AMI/Aptio interface, platform-dependent scope; GM6 вернул только16МиБ BIOS-region|

## 1. flashrom/flashprog: самая широкая основа, не один Windows EXE

Проверены текущие первичные репозитории:

- [flashrom](https://github.com/flashrom/flashrom), snapshot93546a16f2e096eab3f67a015dc7487f81a25e29.
- [flashprog, официальное зеркало SourceArcade](https://github.com/SourceArcade/flashprog),
  snapshotbbe7393f16b710d7e5ddfb1d01c6dadd8c0a5aa7.
- [AMD flashrom-amd](https://github.com/amd/flashrom-amd),
  snapshot642735fa2c31ffbaf50ad492302d3c4089201b39.

flashrom doc/intro.rst:29–32 прямо ограничивает Windows внешними путями без
internal. flashprog README:169–177 и AMD README:145–152 также исключают
Windows PCI/internal. Сторонняя сборка therealdreg/flashrom_build_windows_x64
показывает USB/serial programmers и0 поддерживаемых PCI devices; наличие.exe
не превращает её в чтение системного BIOS без программатора.

Современный Intel не пропущен полностью: flashrom chipset_enable.c содержит
HM7708086:7A0C и pch700 path, но помечен NT; flashprog содержит SPI7A24 как
Raptor Lake-S с configuration-dependent status. Это соответствует сохранённым
ID GM6, не является аппаратным тестом GM6. У flashrom есть отдельные
generation-specific register layouts600/700 series.

Прочитанный flashprog chipset_enable.c:338–350 устанавливает BIOSWE и
пытается снять BLE в общем init. Поэтому один ключ-r не доказывает отсутствие
изменения политики записи при подготовке. Это не означает, что пользователь
попросил или что мы выполнили flash-write: в этой работе вообще не было запуска.
Для собственного read-only backend init следует отделить от enable-write.

### Полезный AMD ROM3 reader

[amd_rom3read.c](https://github.com/SourceArcade/flashprog/blob/bbe7393f16b710d7e5ddfb1d01c6dadd8c0a5aa7/amd_rom3read.c)
реализует memory-mapped чтение. Write/erase отклоняются, есть проверки
адресации, ROM3 baseFD00000000h и соответствия верхних16МиБ ROM3 окнуROM2.
Размер по умолчанию оценивается по повторению содержимого, допускается override.
Оценка размера по повторению не равна JEDEC/SFDP или маркировке чипа.
Полный internal init не равен вызову только этого reader: amd_spi100 также
содержит изменение alternate speed. Разделять reader и programmer init.

Этот код — хороший первичный материал для AMD части общего ядра. На Wujie
нового доступа/проверки этих адресов не было; соседние MMIO-страницы из точного
DSDT нельзя назначать SPI-регистрами по сходному примеру. На Windows ещё нужен
допущенный64-битный transport. Старый Uniwill MMRD с32-битным адресом не получает
ROM3 только от смены типа переменной в вызывающей программе.

## 2. CHIPSEC: полезен, но его dump нельзя считать универсальной гарантией

Текущая default branch —chipsec2, snapshot25457060db45217c74b46f61cc934098ddff8d64.
Проверены [spi_cmd.py](https://github.com/chipsec/chipsec/blob/25457060db45217c74b46f61cc934098ddff8d64/chipsec/utilcmd/spi_cmd.py),
Intel HAL и инструкция Windows.

- spi_cmd.py импортирует chipsec.hal.intel.spi. Наличие AMD64 в инструкции
  Windows означает архитектуруx64, не доказательство AMD SPI backend.
- spi_dump:105–117 берёт размер как BIOS-region limit+1. В самом коде есть
  TODO против предположения, что flash обязательно заканчивается BIOS-регионом.
- Intel read_spi:559–594 при failed chunk пишет ошибку, но не добавляет его
  байты в буфер; следующие успешные байты продолжают добавляться. Полученный
  файл может быть короче и теряет адресную привязку после пропуска, если обёртка
  не проверяет размеры/логи. Допущение «файл создан = полный дамп» неприемлемо.
- Windows instructions включают способы работы с self-signed driver/test mode
  и отдельный PCI filter для SDEV/VBS. Ничего из этого не применялось и не
  предлагается автоматически менять на рабочем ноутбуке.

Как источник Intel SPI-кода/диагностики CHIPSEC полезен. Как готовая подмена
сборщика «любой Intel/AMD в обычной Windows» он не подтверждён.

## 3. rflasher: хорошая архитектура, ранняя реализация

[Репозиторий](https://github.com/ArthurHeymans/rflasher),
snapshot4cf6ecf0e189f0a6e2dd2a76608483f680dff5d1.
README прямо помечает проект alpha, ограничивает область SPI NOR и для
критичного использования направляет к flashprog. Готовый internal использует
Linux PCI sysfs/devmem; host abstraction позволяет написать другой доступ,
но готового Windows host это не создаёт.

В programmer.rs:104–107 Intel constructor без отдельного read-only параметра
пытается enable_bios_write. Его нельзя без аудита вызывать как чистый reader.
В AMD таблице есть790B/revisions, часть статусов Untested. В прочитанной Intel
таблице нет записей7Axx; её таблица/современные stubs не дают автоматического
допуска GM6. Это вывод о конкретном snapshot, не о будущих версиях проекта.

Технически полезны разделение host/controller/chip и no_std controller cores.
Это материал для собственного слоя, не основание сразу заменить рабочий AFU.

## 4. Другие названия, которые не решают полное чтение

- [Google Pawn](https://github.com/google/pawn): snapshot45b7b3865c3d4f2aa6e0defebb477dacb8b8a3b9.
  Требует Linux/devmem, поддерживает Intel ICH8–10/6–9 Series в factory;
  pawn.cc:173–187 фиксирует kMaxFlash16МиБ. Обработчик block read errors в
  Main передаётся как nullptr с комментарием об игнорировании этих ошибок.
  Это отдельный проект, не namazso/PawnIO.
- [Bios-Reader](https://github.com/zabiyakod/Bios-Reader) на проверенной ветке
  содержит только Universal BIOS Backup ToolKit2.0.exe. Исходников, read-map,
  доказательства64-битного полного SPI и допуска драйвера в этой публикации нет.
  Бинарник не скачивался и не запускался; вредоносность или неработоспособность
  этим не утверждается. За основу проверяемого ядра он не выбран.
- Falseclock/UEFI-dumper — обработка входного firmware-файла для извлечения
  модулей/IFR, не live-чтение flash. BIOSUtilities/UEFITool также полезны для
  разбора файлов; файл обновления/наборFV не равен текущему физическому SPI.
- Текущий fwupd BIOS plugin проверяет ESRT/capsule availability, не делает
  полный backup BIOS. MTD plugin работает с тем, что предоставил kernel.
  Наличие общего firmware manager не расширяет аппаратные права чтения.

## 5. Windows API, PawnIO и UEFI

[Microsoft EnumSystemFirmwareTables](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-enumsystemfirmwaretables)
указывает, что FIRM не поддерживается для UEFI; legacy FIRM ranges находятся
ниже1МиБ. RSMB возвращает SMBIOS, ACPI — таблицы. Эти API не предоставляют
пользовательскому процессу полный произвольный SPI/ROM3 image.

В текущем [официальном наборе PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)
snapshot24425a5299e84410751da882c121f322fb030b82 не найден отдельный готовый
SPI/ROM-dump module. Code search SPI нашёл status names/IPsec и LPC/eSPI
комментарии thermal module, не flash reader. [Сайт PawnIO](https://pawnio.eu/)
различает официальный подписанный вариант и unsigned unrestricted edition.
Существующий подписанный driver не означает произвольные unsigned modules.
Новый ограниченный Intel-SPI/AMD-ROM reader и его signing/admission — отдельная
работа. Сборка собственного модуля сама по себе этого не завершает.

[UEFI PI1.9 SPI stack](https://uefi.org/specs/PI/1.9/V5_SPI_Protocol_Stack.html)
описывает EFI_SPI_NOR_FLASH_PROTOCOL с размером/ID/ReadData; EDK2 содержит
соответствующие интерфейсы. При наличии подходящего protocol instance это
полезный общий путь EFI-приложения. Но стандарт интерфейса не доказывает его
публикацию конкретным OEM BIOS или доступность всех регионов. FVB даёт блоки
firmware volume и отдельно не обещает Descriptor/ME/GbE/весь flash.
EFI-загрузка также не является автоматическим снятием chipset read protection.

## 6. Что практически делать в MagicControl

**Одно общее ядро и одна кнопка; несколько проверенных backend по контроллеру
и доступному transport, а не16 скопированных реализаций по названиям моделей.**
Данные/карточки/UI геометрию этот вывод менять не требует.

1. Сохранить рабочий AFU как backend. Wujie уже дал два равных32МиБ образа;
   GM6/O — подтверждённый16МиБ BIOS-region, отдельно от полного SPI.
2. Для совместимого Intel — точный FPT family backend. Наш16.1.25.1871
   [побайтно подтверждён официальным OEM архивом](../fpt-official-origin-20261025/REPORT.md),
   PCH GM6 есть в его таблице; полный runtime-read GM6 ещё не выполнялся нами.
   Intel transport не зависит от того, AMI или Insyde содержит BIOS-region.
3. Для Linux — read-only MTD при наличии нужного kernel device и доступа,
   либо рассмотренный SPI/ROM reader с отдельным init без изменения protection.
   Linux spi-intel-pci содержит SPI-ID7A24 GM6; это source match, не live test.
4. Для собственного Windows native reader — Intel controller generations и
   AMD ROM2/ROM3 под одним API, с допущенным ограниченным kernel transport.
   Готового такого Windows backend у проверенных generic-проектов не найдено.
5. Неизвестная/недоступная область сохраняется честно как partial/unsupported.
   Для полного физического чтения обычного SPI NOR остаётся внешний путь.

[CAPTURE-CONTRACT.md](CAPTURE-CONTRACT.md) фиксирует уже конкретные типы результата,
порядок выбора, чтения и контроля полноты. Это спецификация по итогам исследования,
не утверждение, что новый backend уже подключён/собран/испытан на всех моделях.

## Данные и воспроизводимость

- source-snapshot.json содержит исходный текст41 файла с URL/ref.
- repo-heads.json фиксирует SHA репозиториев и известные Git blob hashes.
- analyze_sources.py материализует только текст, считает SHA256/Git blob SHA1,
  сопоставляет сохранённые hashes; EVIDENCE.json содержит15 точных находок.
- sources/ сохраняет исходные headers/лицензии. Код не импортировался и не
  выполнялся. Ошибка404 старого fwupd flashrom README исключена из источников.
- Ограничение исследования: это source/documentation audit, не проверка всех
  существующих проектов или каждого возможного чипа/ноутбука на оборудовании.
