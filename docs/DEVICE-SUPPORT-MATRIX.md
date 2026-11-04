# Матрица поддержки 17 устройств

Актуализация **27.10.2026**, база сборки **bbd7699**, рабочие Windows/Linux
GUI и mctl **0.1.243**. Файлы/версии/4SHA проверены; в этой документальной
актуализации приложение, драйверы и аппаратные утилиты не запускались.
Подробные датированные evidence ниже сохраняют собственную область проверки.

## Как читать статус

- **Факт** — конкретный результат уже существующего пользовательского опыта или журнала.
  Он относится к указанной версии и функции, а не ко всей модели сразу.
- **Код** — обработчик подключён, но физическое исполнение всей функции не доказано.
  Чтение записанного значения обратно подтверждает передачу настройки, не её эффект.
- **Кандидат** — есть материал для дальнейшего разбора; включать управление ещё рано.
- **Инвентаризация** — модель распознаётся, собственный EC-протокол не подключён.

Реестр: [DeviceRegistry](../src/devices/DeviceRegistry.cpp),
[11 reported-профилей](../src/devices/mechrevo/ReportedProfiles.cpp),
[capabilities](../src/core/include/MagicControl/DeviceProfile.h).
Состав: **7 устройств с собственными Windows путями управления +10 inventory-only =17**.
Это16ноутбуков и mini-PC HPBSD; возможности различаются, полного аппаратного
покрытия каждого элемента нет. Honor не является battery-only профилем.
GM5 входит в 11 reported-профилей, но его возможности уже включены; слово reported
в имени реестра не означает, что все его записи остаются только каталогом.
Unknown/noname — резервный режим, в эти17 не входит.

Меню использует общий каталог. ДобавлениеHPBSD и остальных пунктов не
переносит на них чужой transport/codec. Обычный inventory и общие BIOS/VBIOS
readers имеют отдельные hardware admissions: inventory-only EC профиль не
означает запрет любого INFO reader и не означает разрешение EC writer.
Текущее устройство/версия reader важнее названия выбранного preview.
Сводка и reader evidence: [PROJECT-MAP](PROJECT-MAP.md).

## Общие функции для всех моделей

Доступны общие страницы, системная инвентаризация и те поставщики CPU/GPU/дисков/
батареи, которые реально поддерживают ОС и устройство. Это не обещание всех датчиков
на каждом ноутбуке. CPU/SMBus/MSR по-прежнему требуют собственных аппаратных проверок.

[KEYs](../src/gui/common/KeysPolicy.h) не скрывается по модели: OSD и системный
микрофон не требуют её EC-карты. В Windows также используются существующие системные
пути Wi-Fi/Bluetooth/камеры/тачпада и программная блокировка Copilot. В Linux эти
четыре системных переключателя и Copilot пока не подключены. Windows/Fn/USB/AC
через Wujie EC не переносятся на остальные платы; Honor Fn использует свой HWMI/sysfs.
Состояние и доступность определяются ответом ОС; выбор чужого профиля — просмотр.
Аппаратная работа микрофона на всех устройствах отдельно не проверялась.

## Основная таблица

«Можно дальше» ниже означает **работу над кодом и анализом файлов**. Новые аппаратные
команды или включение capabilities этой таблицей не разрешаются.
Пакеты данных **D0–D3** описаны после таблицы.

| № | Точная плата / модель; известный BIOS | Что уже есть и что подтверждено | Что можно делать по имеющимся материалам | Чего не хватает / нужен ли новый дамп |
|---|---|---|---|---|
| 01 | **WUJIE Series-X5SP4NAG**, Wujie15XPro; N.1.20MRO15 / EC1.18 | FANs/PL/SingleZone/батарея/KEYs; AFU два32МиБ ROM; own embedded EC candidate и voltage-aging цепочка разобраны. [M01](#m01-wujie) | Native CPU policy/Auto/Custom по своему codec; native battery Full Capacity/Balanced/Healthy отдельно от процентов. | Candidate из BIOS не доказывает liveEC flash. Для автономного95/90 нужны соответствие прошивки и наблюдение зарядного цикла; не новый общий RAM dump ради повторения. |
| 02 | **HLYL-WXX9-PCB / HLYL-WXX9**, Honor; BIOS1.09 / EC1.9 | Собственные HWMI/I2EC/FAN/SMU/зарядка/подсветка; InsydeH2OFFT6.76/SYS3.0.0.3:0.1.239, два одинаковых16МиБ ROM. [M02](#m02-honor) | Развивать typed Huawei пути, сохраняя guards. Общий UI профилей не означает автономную Uniwill RAM кривую. | Full physicalSPI/liveEC flash и поведение других Honor не подтверждены. Linux — только реальные sysfs интерфейсы. |
| 03 | **GM6IR0C / JiguangX Series GM6IR0C**; N.1.05MRO09 / EC1.12 | Код RAM1.5 FANs/online, native профилей и кнопки, Intel PL1/PL2/PL4, RGB, батареи. Факт: RPM/PWM, Intel DTS/RAPL/NVIDIA, Max с возвратом; в 0.1.162 повторяемая реакция зарядки на верхний 95/85 при SOC88. [M03](#m03-gm6ir0c) | Продолжать точный OEM/EC1.11–1.12 разбор Healthy, текущей телеметрии и сохранения. Проверять код нижнего поля отдельно от поиска firmware-потребителя. | **D1/D2 уже есть**; повторный общий дамп не является первым шагом. Для нижнего start и восстановления после reboot/resume нужны целевые журналы; полный цикл 95→90 при неизменной настройке не подтверждён. |
| 04 | **GM5HG0A / Yilong15Pro Series GM5HG0A**; N.1.09MRO19 / EC1.15 | Код RAM1.5 FANs/online, AMD SPL/SPPT/FPPT, RGB, upper и native battery policy. Факт 0.1.146: Max **4823/4673 RPM**, возврат; Manual/PL/RGB имеют readback. Процентная отсечка не проверена на заряжающейся батарее. [M04](#m04-gm5hg0a) | Использовать свой OEM5.52.50.0 и общий контроллер; уточнять датчики и физическую кнопку. Код чтения DIMM/температуры батареи исправлен в 0.1.147, для него нужен свежий ответ ноутбука. | **Полный RAM/ACPI/драйвер уже есть**. Нужны целевые логи новой версии, а не повторная инвентаризация. Для процентной отсечки — наблюдение зарядки. `7D0/7D1=DBD1/DBD2` не включать как restart. |
| 05 | **MACHENIKE / L16 / L16**, L16 S, Ryzen9 7945HX; N.1.13MAC10 / EC1.16 | Код общих FANs/online, AMD CPU-only, RGB, батарейной телеметрии/upper. Факт 0.1.152: Max **5208/5205 RPM**, возврат Default; полные RAM-снимки. Платформа6 сохраняется. [M05](#m05-l16) | Дорабатывать чтение/интерпретацию новой батареи и имеющиеся независимые FANs/CPU команды. Сверять пригодные DefaultTool/константы OEM5.60.60.17. | **D1 уже есть**. Для штатного полного цикла профилей и точного firmware-поведения нужен **D3 MAC10/EC1.16** либо восстановленные тела OEM; защищённые stubs недостаточны. Для Manual/online/отсечки нужны целевые наблюдения. |
| 06 | **Eluktronics Inc. / HYDROC-16 / HYDROC-16 G2**; N.1.13ELU11 / EC1.12 | Код: общий typed RAM1.5 FAN/CPU PL, guarded ITE HID зоны, прежний native battery policy и диагностика. BIOS/driver/P13/LC/descriptor/FW проверки обязательны. Физическую работу новых HYDROC writers и процентную отсечку не считать доказанными. [M06](#m06-hydroc) | Точный OEM5.23.51.56 уже позволяет разбирать выбор FAN-формата, CPU PL и ветви HID/EC. Подготовить проверки идентичности/дескрипторов и read-only декодеры. | Есть исходный дамп, но для клавиатуры нужен **D2**: фактические HID VID/PID/Usage/caps и версия/ветвь протокола. Для FANs — соответствие штатных таблиц выбранному OEM-пути. Перенос SingleZone запрещён по старому `766.2=0`. |
| 07 | **GM6PX7X**, Kuangshi16 Super; i9-13900HX/RTX4070; N.1.06MRO02 | Инвентаризация; protocol/capabilities пусты. Из отчёта ProjectID16, CC5.56.60.4. [I07](#i07-gm6px7x) | Общие системные функции и отдельный пункт меню. Подготовить разбор именно его OEM: Project16 сам не выбирает legacy/RAM1.5. | В привязанном отчёте нет ACPI/RAM. Нужен **D0**, затем **D1** через проверенный reader; перед FANs/PL/RGB/battery — свой OEM ABI. |
| 08 | **KUANGSHI-JIGUANG Series-X6xx55xW-B2**, KUANGSHI; i5-14450HX/RTX5060; N.1.21MRO16 | Инвентаризация; EC-протокол пуст. Отчёт Project26, CC5.56.60.26. [I08](#i08-kuangshi-55) | Общие функции; анализ OEM и родного выбора ветви. Сходство с X6xx57TW/Project26 не заменяет проверку. | **D0+D1**: собственные ACPI, RAM/validity, capability и установленный драйвер; сверка варианта CPU/GPU и ProductName. |
| 09 | **CANGLONG Series-X6xR55xY**; Ryzen9 9955HX/RTX5070Ti; N.1.12MRO14 | Инвентаризация; EC-протокол пуст. Два отчёта одной BIOS-версии, Project26, CC5.56.51.62/5.56.51.44. [I09](#i09-canglong-x6) | Общий мониторинг при поддержке конкретного CPU. Подготовить AMD/EC-карту по собственному OEM, не копировать L16 пределы/кодек по Ryzen9. | **D0+D1**; отдельно подтвердить поддержку CPUID/SMU этой Zen5-конфигурации. Точного MRO14-образа в выбранной EC-матрице нет. |
| 10 | **GM6BG0X**, Jiaolong16K; Ryzen7 7435H/RTX4060; N.1.26MRO24 | Инвентаризация; EC-протокол пуст. Project16; версия CC в индексе неизвестна. [I10](#i10-gm6bg0x) | Общие функции. Первым делом восстановить привязку к OEM-пакету и формат FANs; AMD CPU не означает карту GM5. | **D0+D1**, особенно точный CC/DefaultTool/GCUService и проверка capability. `MRO24` не связывает эту плату с YAOSHI. |
| 11 | **KUANGSHI-JIGUANG Series-X6xx57TW**, JIGUANG; i7-14650HX/RTX5070Ti; N.1.35MRO58 **и** N.1.21MRO16 | Инвентаризация; EC-протокол пуст. Два отчёта разных BIOS, Project26, CC5.56.51.44. [I11](#i11-jiguang-57) | Приоритетный статический кандидат: в корпусе есть **XxRPxxxxN135MRO58**, разобраны выбранные counter/charge ветви. Сначала доказать соответствие семейства и варианта. | **D0+D1 для фактической ревизии**. Совпадение N135MRO58 — зацепка, не полное подтверждение payload. Вариант N121MRO16 проверять отдельно. |
| 12 | **JIAOLONG Series-X6DR55xx-B2**; Ryzen9 8940HX/RTX5070; N.1.19MRO20 | Инвентаризация; EC-протокол пуст. Project26, CC5.56.51.53. [I12](#i12-jiaolong) | Семейный **X6DR5xxKN140MRO56** и FanSlider дают материал для анализа RAM1.5/зарядных ветвей. FanSlider испытывался на иной конфигурации — не подтверждение этой платы. | **D0+D1**, точная связь N119MRO20 с пакетом/EC. Не переносить N140MRO56 и калибровку 8945HX/5070Ti на 8940HX/5070. |
| 13 | **GX6HG0U-F**, Huan16 Air; Ryzen7 8745H/RTX4060; N.1.14FIR15 | Инвентаризация; EC-протокол пуст. Project26, CC5.49.51.44. [I13](#i13-huan) | Общие функции; разбор собственного FIR-пакета. Два найденных **GXxHXxxN114MRO50** — только сравнение реализации, не готовая карта этой платы. | **D0+D1**. Различать `GX6HG0U-F/FIR15` и `GXxHXxx/MRO50`; похожие GX/N114 не доказывают идентичность. |
| 14 | **CANGLONG Series-X8xR55xP-B2**; Ryzen9 9955HX/RTX5070; N.1.40MRO57 | Инвентаризация; EC-протокол пуст. Project26, CC5.56.60.34. [I14](#i14-canglong-x8) | Общие функции; сравнить OEM с X6, сохраняя отдельный профиль и допуски. | **D0+D1**. Не использовать MRO14 от X6 или MRO56 от X6DR по одному префиксу/номеру; своё EC/RAM и AMD-кодек. |
| 15 | **YAOSHI Series-X6AR55xY**; Ultra9 275HX/RTX5070Ti; N.1.18MRO24 **и** N.1.33MRO62 | Инвентаризация; EC-протокол пуст. Два отчёта, Project26, CC5.56.60.22/5.56.51.44. [I15](#i15-yaoshi) | Приоритетный семейный кандидат: разобран **X6AR5xxxN133MRO63**, рядом есть X6AR45-вариант. Есть общий Intel C5/C6 read-only monitor, но его наличие не даёт EC-карту. | **D0+D1 отдельно для MRO24/MRO62**; доказать соответствие ветви. MRO63 не является точным MRO62, X6AR45 не равен X6AR5. |
| 16 | **GM6IX9B**, Yaoshi16Super; i9-14900HX/RTX4090; N.1.10MRO09 | Инвентаризация; EC-протокол пуст. Project25, CC5.56.51.58. [I16](#i16-gm6ix9b) | Приоритетный кандидат: OpenRevo прямо упоминает **GM6IX9B** в модели `models.json`; общие Intel и RAM1.5 механизмы можно сопоставлять с его OEM. | **D0+D1**, собственные FAN-таблицы/PL/драйвер. Публичный OpenRevo не содержит аппаратных карт; пример PL из документа не переносить. Project25/MRO09 не делают его GM6IR0C. |
| 17 | **MINISFORUM EliteMini HPBSD**, Ryzen7 8845HS/Radeon780M; AMI1.06 / EC0.2 | Собственный ITE5571/PawnIO FAN controller и SMU telemetry; AFU два32МиБ ROM. [M17](#m17-hpbsd) | Сохранять отдельную native policy, ownership/Default и исходные кривые. Границы GUI20–100°C/5–100% не изменяют аппаратные таблицы сами по себе. | Public CPU power setter/battery/RGB/Uniwill map не включены; Linux writer не подтверждён. SPD temperature unavailable не доказывает отсутствия датчика. |

## Что означает «нужен дамп»

| Пакет | Содержимое | Для чего нужен |
|---|---|---|
| **D0 — исходная идентичность и OEM** | Точные manufacturer/baseBoard/product, BIOS/EC-ревизия, CPU/GPU; ACPI DSDT/SSDT штатными API; версия/хэш установленного SYS; оригинальный OEM-пакет и пригодные тела методов | Сначала выбрать настоящий транспорт и протокол. Старые JSON hardware-report дают в основном инвентаризацию, перечень служб/устройств/WMI и CC registry; это не ACPI AML или 4KiB RAM. |
| **D1 — пассивный снимок EC RAM** | `ec.bin` **и** `ec-valid.bin`, адреса/длина, timestamp, фактический интерфейс, ошибки; два стабильных чтения identity/capability, штатные FAN-таблицы если они адресуемы | Сопоставить OEM-карту с реальной платой. Непрочитанные байты не трактовать как нули. Нужен только уже проверенный для платы read-only путь. |
| **D2 — специальная периферия/наблюдение** | Для HYDROC — HID instance/дескрипторы/firmware-ветвь; для существующих контроллеров — журналы штатной кнопки, питания и телеметрии | Закрыть конкретный пробел без повторного общего сбора всего подряд. Команды FW/HID, если они требуются, должны быть отдельно классифицированы: они не равны простому чтению дескриптора ОС. |
| **D3 — исполняемая прошивка** | Оригинальный BIOS/EC-пакет либо файл, из которого выделяется нужный EC-код с установленной ревизией и SHA | Разобрать алгоритм, которого нет в доступном точном образе. RAM 000–FFF, DSDT и SPI BIOS без нужного EC payload не являются заменой. |

Перед просьбой прислать новое сначала проверить существующие `Users_logs`,
`research/laptop-reports/telegram-originals`, индексы распакованных OEM и Reference_Projects.
В десяти строках инвентаризации отмечено отсутствие **доказанной связки этой платы
с ACPI/RAM/OEM**, а не доказано отсутствие любого подходящего файла во всех архивах.

**В этой задаче D0–D3 не собирались на железе.** Обычный INFO-дамп на поддержанной
плате может выполнять Default/Max: см. [контракт диагностики](DIAGNOSTIC-DUMPS.md).
Поэтому просьба «нужен дамп» здесь не означает «нажать обычную кнопку с активными
пробами». Для будущего сбора без записей требуется явный ReadOnly/пассивный путь;
на неизвестной плате даже чтение полного окна сначала требует допуска транспорта.

## Источники для шести подключённых моделей

### M01 Wujie

- [Profile](../src/devices/mechrevo/x5sp4nag/Profile.cpp), [README](../src/devices/mechrevo/x5sp4nag/README.md).
- Текущие контракты: [FANs](../src/platform/windows/FAN_MODES.md),
  [мощность](../src/core/src/POWER_MODES.md), [батарея](../src/platform/windows/BATTERY.md).
- Ночной результат: [сохранённая выжимка уже записанного лога](../research/device-support-matrix-20261024/wujie-night-evidence.json).
  24.10 в00:47:46 МСК — idle на85%; до05:58:28 —1765 периодических
  снимков85%/0Вт, максимальный промежуток12.728с. Затем AC отключён,
  в06:00:12 SOC85→84. Это наблюдение верхнего порога, **не доказательство start95/90**.
- В собственном32МиБ BIOS извлечён embedded EC core candidate128КиБ,
  SHA5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8.
  Разобраны native voltage/age handlers. Это не отдельное чтение исполняемого
  EC flash: его точное соответствие и физическое исполнение проверяются отдельно.

### M02 Honor

- [Profile](../src/devices/huawei/hlyl-wxx9/Profile.cpp), [README](../src/devices/huawei/hlyl-wxx9/README.md).
- [Аппаратные результаты0.1.28–0.1.37](../research/devices/hlyl-wxx9/honor-20261018/RESULTS-20261018.md).
  В частности, есть физическая остановка при70/80 и возобновление при выборе95/100;
  это не наблюдение полного автономного гистерезиса start/end.
- В Windows подсветка — IT8987 I2EC, HWMI SLIV с ложным успехом не применяется.
  FANs использует собственные PWM/guards и общий UI профилей, без переноса RAM-кривых Uniwill.
- BIOS reader6.76 и SYS3.0.0.3 проверены в0.1.239: два16МиБ ROM, SHA75c813b7...46dd445.
  Evidence: research/insyde-update-20261027/VERIFICATION-239.json; прежняя6.25 —0.1.238.
- [Linux HuaweiControl](../src/platform/linux/huawei-wmi/HuaweiControlLinux.cpp):
  charge start/end, Fn, штатный LED и timeout только при наличии нужных sysfs/прав.
  Это подключённый backend, не аппаратное испытание Linux на всех версиях ядра.

### M03 GM6IR0C

- [Profile](../src/devices/mechrevo/gm6ir0c/Profile.cpp), [README](../src/devices/mechrevo/gm6ir0c/README.md),
  [разбор0.1.162](../research/gm6ir0c-logs11-20261023/REPORT.md).
- Архив11 приSOC88 содержит повторные95→charging /85→Reached→0mA.
  Команды/readback и реакция телеметрии подтверждены; независимая калибровка тока не выполнена.
- В том же разборе зафиксирована подмена lower50→80 в applyUpper при fallback.
  Это дефект передачи настройки, отдельный от неизвестного firmware-потребителя lower.
  Исправление здесь не выполнялось; перед дальнейшей работой проверить актуальный код.
- Есть EC1.11/1.12 и несколько BIOS/SPI вариантов. Их происхождение, payload и
  установленная версия должны сопоставляться по SHA; одно совпадение MRO09 недостаточно.

### M04 GM5HG0A

- [Profile](../src/devices/mechrevo/gm5hg0a/Profile.cpp), [README](../src/devices/mechrevo/gm5hg0a/README.md),
  [свежий разбор архива4](../research/gm5hg0a-logs4-20261022/REPORT.json).
- Старый README говорит об отсутствии успешных EC-чтений в первых архивах.
  Архив4 новее: OEM освободил управление, Max стабилизировался и был возвращён;
  RAM Manual и CPU-тройки прочитаны обратно, цветовые команды подтверждены readback.
- Батарея в архиве4 всё время100%/0mA. Из этого нельзя получить доказательство cutoff,
  даже если верхний байт и Reached менялись. Полное отслеживание Manual/online по температуре
  и нажатие физической кнопки также не доказаны.

### M05 L16

- [Profile](../src/devices/machenike/l16/Profile.cpp), [README](../src/devices/machenike/l16/README.md),
  [разбор3.zip/4.zip](../research/users-logs-review-20261023/REPORT.md).
- Во втором опыте OEM остановлен, Max100% выдержан30с и восстановлен Default.
  Это сильнее старой формулировки «подключено без проверки», но не подтверждает все режимы.
- Между3.zip и4.zip владелец **заменил батарею**:82→8 циклов не считать сбросом BMS
  одной батареи. XIF0=0 у новой батареи всё ещё не даёт объявить raw4300 мА·ч/здоровьем.
- Factory mailbox/штатный полный цикл профилей недоступны; аппаратный backend
  сохраняет platform6 и не заимствует7/04 от GM6.

### M06 HYDROC

После ревизии подключены [общая карточка зон и guarded FAN/PL/ITE](LIGHTING-ZONES.md). Далее исторический срез0.1.172: собственный dump
содержитP13/ROMFF иPL4double. Обычные FANs/PL/HID writers выключены, но активный
INFOWithFanProbes уже имеет отдельные Default/factory записи; только RAM-reader
и явно ReadOnly-сбор являются пассивными. Старое краткое описание ниже читать
с этими уточнениями. Переключатель4/7 в исходномP13 блокируется guard.

- [Profile](../src/devices/eluktronics/hydroc-16-g2/Profile.cpp), [README](../src/devices/eluktronics/hydroc-16-g2/README.md),
  [OEM control review](../research/devices/hydroc-16-g2/oem-5.23.51.56/CONTROL-REVIEW-20261019.txt).
- Собственный OEM восстановлен; batteryCharge — единственная управляющая capability.
  Native Healthy/Balance/Performance — политика7A6, не три доказанных процентных порога.
  Поддержка trial upper/lower и platform/ROM-переключения в коде не доказывает отсечку.
- HID-кандидаты — описание дескрипторов, `permission=false`; FW80, actual instance
  и выбор keyboard/lightbar требуют отдельной привязки. Чтение RAM само не выбирает FAN writer.

## Источники и точные пробелы десяти остальных профилей

Для всех строк ниже первичный локальный индекс —
[hardware reports](../research/laptop-reports/index.json),
[читаемая инвентаризация](../research/laptop-reports/README.md).
Оригиналы находятся в `research/laptop-reports/telegram-originals`.
Номера BIOS/CC/Project в таблице — сведения этих отчётов, не live-проверка сегодня.

### I07 GM6PX7X

[Профиль/границы](../src/devices/mechrevo/gm6px7x/README.md).
Отчёт `Mechrevo-Hardware-Report-20260904-103150.json`.
Связанный OEM-декомпил для точной installed-ревизии в этой выборке не установлен.

### I08 KUANGSHI 55

[Профиль/границы](../src/devices/mechrevo/kuangshi-jiguang-series-x6xx55xw-b2/README.md).
Отчёт `Mechrevo-Hardware-Report-20260904-185700.json`.
Проверять отдельно от модели57TW даже при общей версииN.1.21MRO16.

### I09 CANGLONG X6

[Профиль/границы](../src/devices/mechrevo/canglong-series-x6xr55xy/README.md).
Отчёты `20260904-215024` и `20261006-110709`.
Повторный hardware-report не является вторым подтверждением EC-протокола.

### I10 GM6BG0X

[Профиль/границы](../src/devices/mechrevo/gm6bg0x/README.md).
Отчёт `Mechrevo-Hardware-Report-20260905-151120.json`.
Сначала получить/найти связанный OEM и ACPI; не выводить формат таблиц изProject16.

### I11 JIGUANG 57

[Профиль/границы](../src/devices/mechrevo/kuangshi-jiguang-series-x6xx57tw/README.md).
Отчёты `20260905-173125` и `20260924-093521`.
[Матрица выбранных firmware-consumers](../research/ec-platform-catalogue-20261023/PLATFORMS-5-6-7.md)
содержит XxRP N135MRO58, но нет доказанной полной связи payload с обеими reported-ревизиями.

### I12 JIAOLONG

[Профиль/границы](../src/devices/mechrevo/jiaolong-series-x6dr55xx-b2/README.md).
Отчёт `Mechrevo-Hardware-Report-20260920-002602.json`.
[Локальный FanSlider](../../Reference_Projects/FanSlider/repository/README.md) заявляет
опыт на8945HX/5070Ti, ITE EC-V14.6. Это не тождественно нашему reported8940HX/5070.
[Его FanProtocol](../../Reference_Projects/FanSlider/repository/FanProtocol.cs) —
материал для сопоставления RAM1.5, не готовый допуск платы.

### I13 HUAN

[Профиль/границы](../src/devices/mechrevo/gx6hg0u-f/README.md).
Отчёт `Mechrevo-Hardware-Report-20260927-165101.json`.
[Два GX EC из MRO50](../research/ec-platform-catalogue-20261023/additional-source-units.json)
в исследовании имеют собственные SHA и неизвестный выбор аппаратного варианта;
они не объявлены FIR15 этого Huan.

### I14 CANGLONG X8

[Профиль/границы](../src/devices/mechrevo/canglong-series-x8xr55xp-b2/README.md).
Отчёт `Mechrevo-Hardware-Report-20260929-114539.json`.
Связка exactX8/MRO57 с исполняемым EC и FAN-ABI пока не установлена.

### I15 YAOSHI

[Профиль/границы](../src/devices/mechrevo/yaoshi-series-x6ar55xy/README.md).
Отчёты `20261002-181916` и `20260904-122525.zip`.
Последний — только JSON-инвентаризация внутри ZIP.
[Каталог образов](../research/ec-platform-catalogue-20261023/source-units.json)
содержит X6AR5 N133MRO63 и отдельный X6AR45 N115MRO10; выбранные условия
зарядки/индикации изучены, но exactMRO24/MRO62 и его полный host-ABI не доказаны.

### I16 GM6IX9B

[Профиль/границы](../src/devices/mechrevo/gm6ix9b/README.md).
Отчёт `Mechrevo-Hardware-Report-20261003-113530.json`.
[Локальный OpenRevo models guide](../../Reference_Projects/OpenRevo/repository/docs/models_customization_guide.md)
содержит GM6IX9B и пример factory/presetPL.
[README OpenRevo](../../Reference_Projects/OpenRevo/repository/README.md) прямо говорит,
что публичный набор не содержит proprietary mappings/hardware probes.
Не считать пример параметров восстановленным кодом и не назначать по нему лимиты.

## Что можно подключить следующим

1. **Меню/просмотр16профилей:** подключено отдельной UI-правкой24.10,
   с честными состояниями поддержки. Нового аппаратного протокола нет.
2. **Статический приоритет среди новых плат:** GM6IX9B (прямое имя в OpenRevo),
   JIGUANG57TW/N135MRO58 (кандидат по версии корпуса), YAOSHI/N133MRO62
   (родственный X6AR5 MRO63). Приоритет означает удобство поиска, не подтверждённую совместимость.
3. **У уже подключённых:** GM5/L16 имеют свежие физические FANMax-доказательства;
   дальше закрывать конкретные пробелы Manual/online/CPU/battery, сохраняя их собственные
   настройки и проверки. HYDROC требует выбора собственного FAN/HID-пути.
4. **Для Linux:** общий интерфейс уже есть. У Uniwill-моделей отсутствует
   [подключённый EC transport](../src/platform/linux/BackendLinux.cpp), а у Honor
   есть отдельный sysfs backend. Документация по Windows IOCTL сама Linux writer не создаёт.

### M17 HPBSD

Mini-PC, отдельный Profile.cpp/ITE5571 backend. Exact manufacturer/board/product,
BIOS/CPUID/ID guards, NativeFixedFans, собственные receipts и Default recovery.
FANs и SmuTelemetry включены; публичные PowerLimits/Battery/Backlight не включены.
После0.1.237 исходные кривые сохранены: не возвращать промежуточное
пересэмплирование ради полей20–100°C/5–100%. Evidence и границы:
[src README](../src/devices/minisforum/hpbsd/README.md),
[профиль](../src/devices/minisforum/hpbsd/Profile.cpp),
`research/minipc-hpbsd-20261026/followup`. BIOS AFU32МиБ two-pass,
SHA c7c0247ae493ccae23cc790de9acc4f1bbd905953dbe688b08caa4af4bc4a379.

## Общие ограничения, которые таблица не снимает

- 23компонента firmware-корпуса (10EC+13BIOS) не равны23ноутбукам и не покрывают
  точные ревизии всех17профилей. Актуальная привязка:
  [CORPUS-STATUS](../research/ec-platform-catalogue-20261023/CORPUS-STATUS.md),
  [GOAL audit](../research/ec-platform-catalogue-20261023/GOAL-REQUIREMENTS-AUDIT.md).
- Нижний Uniwill95/90 неизвестен. Native Huawei start/end — отдельный API;
  даже там полное автономное удержание/возобновление нужно наблюдать отдельно.
- P7/ROM4 не эквивалентP4/ROMFF: platform/ROM участвуют также в подсветке,
  FAN/числовых/BIOS-ветвях. Не переключать их для «проверки совместимости».
- У новых моделей не отключаются OEM-службы, не устанавливается другой SYS,
  не исполняются скачанные DLL/EXE и не подменяются firmware/калибровки.
- Текст некоторых README исторический: свежие результаты выше не переписывают
  старый опыт, но имеют приоритет при оценке конкретно проверенной функции.

Исходный исследовательский срез — документация и чтение существующих файлов.
Отдельная UI-правка меню24.10 описана в [селекторе](LAPTOP-SELECTOR.md);
её сборка не расширяет аппаратные разрешения. Новых аппаратных опытов для этой
матрицы не проводилось.
