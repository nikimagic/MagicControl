# MECHREVO JiguangX Series GM6IR0C

Профиль добавлен 18.10.2026. Поисковые торговые названия: MECHREVO Aurora X2024,
Aurora X2024 Extreme и китайское 极光 X / 极光X2024, обязательно с уточнением
i7-14650HX / RTX4060. Пользователь уточнил английское название20.10.2026;
продавец LaptopAZ также называет эту конфигурацию Aurora X2024 Extreme.
В меню/шапке отображается JiguangX GM6IR0C без служебного слова Series
(правка подписи20.10.2026, общая для Windows/Linux).
SMBIOS-имя JiguangX Series GM6IR0C и проверка платы GM6IR0C остаются точными:
одно торговое имя не определяет аппаратную ревизию. Пользовательская конфигурация: i7-14650HX и RTX4060
Laptop, BIOS N.1.05MRO09, EC1.12. Доступа к ноутбуку у разработчика нет.

## Статус

Реализованы команды по активному OEM-коду и сохранённым таблицам именно этой
платы. Журнал0.1.59 подтвердил EC-чтение, readback команд и пробу Max с возвратом.
Реакция нового Manual online требует проверки на ноутбуке.
Windows: прямой Uniwill ACPIDriver, собственный typed Gm6Controller; MQTT,
GCUBridge и OEM DLL не нужны. Уже установленный OEM-драйвер должен открывать
`\\.\ACPIDriver`. Встроенный SYS не означает автоматическую установку драйвера.
Linux: профиль распознаётся; EC-записи остаются недоступны без собственного
подтверждённого транспорта. Наличие профиля не включает Windows IOCTL на Linux.

При переключении вкладок/выходе нет применения настроек. --no-ec
запрещает доступ. FANs/SETs делят один контроллер и transaction gate.
Generic EC-фабрика намеренно не отдаёт эту плату старым X5-обработчикам:
нет X5 startup restore, AMD SMU, батарейных флагов4/7 или RGB-записей.

Штатная защита батареи19.10.2026: отдельный typed API выбирает0/1/2
Производительность/Баланс/Здоровье, masked7A6.4–5 с readback/rollback.
Это не проценты100/95/85. Подтверждённый выбор сохраняется отдельным ключом
BatteryProtection в gm6ir0c-device-settings.ini; только при наличии этого ключа
выполняется узкая проверка/восстановление после запуска и сна. Нет записей FANs,
PL или платформы4/7 из батарейного API, нет сброса при выходе. Проверка физического
зарядного тока ещё не выполнена. OEM-основание: BATTERY-OEM-REVIEW.txt.

Допуск: точные manufacturer MECHREVO, baseBoard GM6IR0C и product
JiguangX Series GM6IR0C, затем EC0x740=25 и 0x78E.6=1. Эти признаки повторно
читаются перед записью. ProjectID25 не равен BIOS_PROJECT_ID.IDC.
Во время работы GCUService.exe/AiStoneService.exe доступно только чтение.
Приложение не останавливает службы самостоятельно.

## FANs

OEM FanInfo: CPU RPM=0x464:0x465, **GPU RPM=0x46C:0x46B**, PWM0x75B/0x75C
делится на2. Для тахометра проверяется high-low-high; неверный/меняющийся
показатель недоступен. Нельзя подставлять GPU low0x46D от X5SP4NAG.

Ручные независимые16-точечные CPU/GPU таблицы:
CPU0xF00/0xF10/0xF20, GPU0xF30/0xF40/0xF50. Up[i]=point[i+1].UpT,
Up[15]=255; Down[i+1]=point[i].DownT, Down[0] сохраняется; Duty[i]=2*percent.
UpT/DownT не сводятся к общему гистерезису3°C. OEM Office имеет особые пороги.
UI использует общие карточки FANs,8точек на страницу, UpT/PWM и исходный
общий скроллбар. При изменении UpT сохраняется смещение соответствующего DownT
из OEM-таблицы; служебные255 не превращаются в температурные точки. График является
черновиком; фактические RPM/PWM выводятся отдельно. Up[0]/Down[15] не записываются.
Все ручные кривые загружаются/сохраняются общим fan-curves.csv, как на Wujie.
Старый gm6ir0c-fans.txt не подменяет этот файл. Загрузка не применяет режим EC.

SETs использует общие карточки батареи/производительности/подсветки. Intel поля
PL1/PL2/PL4 расположены в обычном ряду с Manual. Вне редактирования серые числа
показывают прочитанные EC overrides; первый Manual открывает свой сохранённый
черновик, второй применяет. Черновик gm6ir0c-power.txt не смешивается с AMD INI.
При занятом OEM управлении редактор закрыт, реальный выбранный режим подсвечен.
Чёрный OSD Office/Gaming/Turbo принадлежит OEM; наши операции используют OsdPopup.

Применение: снять0x7C6.2, записать таблицы и режимные поля, поставить0x741.0,
последним включить0x7C6.2. Manual:0x7C5.7=1 (раздельные каналы),
0x727.6=1,0x726.7=1, mode0x751.7/.5/.4=0 и Fan Boost=0.
Max меняет только0x751.6; Default снимает commit/custom/override/Boost,
передавая управление внутренней политике прошивки без подмены RAM-таблиц.
Профиль SETs и Intel overrides сохраняются. Ни один режим не
сбрасывает таблицы нулями. При выходе нет переключения Default.

## SETs / Intel

Родные Office/Gaming/Turbo OEM являются комплексными режимами и связаны с FANs.
Это источник протокола, но не разрешение менять вентиляторы из нашей CPU-карточки.
SETs использует отдельный CPU-only API `setCpuPreset(0/1/2)`:
Офисный/Сбалансированный(Gaming)/Турбо — application PL presets, которые свежо
читают PL1/PL2/PL4 из734/735/736,730/731/732,7A7/7A8/7A9 соответственно
(active MyFanManager_RamFan1p5.GetOffice/Gaming/TurboPLDefaultValue).
Архивные45/45/200,75/75/200,140/165/250 подтверждают происхождение данных;
они не зашиты как fallback и не выдаются за измеренные/гарантированные ватты.
При недоступных/невалидных firmware defaults соответствующая кнопка недоступна.
Применение от подтверждённой сети пишет только overrides783/784/785 с полным
readback. Оно не вызывает stopOnline, не пишет751, RAM, commit/custom/light,
respective или другие FANs-поля. Старый API setMode делегирует сюда же.
Gm6State.mode остаётся наблюдением native whole-system751; отдельный cpuPreset
определяется только однозначным совпадением actual overrides с прочитанными
PL triples. Нулевые overrides, другие значения или неоднозначность дают-1.
Чтение и создание UI не применяют CPU-пресет; после сна он не навязывается.

Intel override0x783/0x784/0x785=PL1/PL2/PL4. Ноль — штатная политика, не0Вт.
Manual от сети: PL1 10–140, PL2 10–165, PL4 10–250, PL1≤PL2≤PL4.
Верхние границы взяты из OEM Turbo статуса этого пакета, не из измерения
эффективных лимитов процессора. При0x727.7=1 ручная запись отключена:
масштаб PL4 этой ревизии не подтверждён. Сброс overrides к0 доступен.
Readback подтверждает EC, **не MSR/RAPL или физическое потребление CPU**.
Транзакции ведут журнал изменённых полей и при ошибке пытаются откатить только
поля, всё ещё содержащие наше значение, сохраняя соседние биты.
Manual сохраняет gm6ir0c-power.txt внутри typed backend через общий атомарный
savePortableText, после readback всей тройки. Ошибка записи/финализации/замены
явно сообщается; собственные PL откатываются, только если значения ещё наши.
Появление OEM/смена identity блокируют rollback; неподтверждённый возврат
попадает и в ошибку операции, и в журнал. CPU presets не заменяют Manual draft.
loadPowerDraft читает прежний формат3целых, проверяет границы/полноту и размер;
без файла нет выдуманной фабричной тройки. Нет автоматического CPU startup restore.

Батарейная native policy подключена отдельным NativeBatteryController, см. выше;
процентный предел не подтверждён. RGB SingleZone подключён к общему ядру;
визуальная проверка новых команд MagicControl владельцем ещё требуется.

## Источники

Пользовательский ControlCenterX-Logs-20261017-215646.zip, вложенный
Mechrevo-Research-GM6IR0C-20261017-215647.zip,504файла. Активный стек:
Control Center/UniwillService/GCUService1.0.2.70.

SHA256 GCUService-active.exe:
8FD02BF5F42629998D8EC716C3E6FF906BAF73CB356B1FDD7585A26D77A49DAF.

Методы: MyFanCtrl ctor, MyEcCtrl.GetProjectIdFromEC/IsSuportRamFan1p5,
FanInfo.GetEcCpuFanRpm/GetEcGpuFanRpm/GetEcCpuFanDuty/GetEcGpuFanDuty,
MyFanManager_RamFan1p5.SetFanMode/SetFanBoost/SetPL1Value/SetPL2Value/SetPL4Value,
MyFanTableCtrl.SetFanControlByRamFan1p5/CustomerModeLightOn/Off,
FanTable_Manager1p5.SetEcFanTable/SetEcFanControlRespective.

SHA256 исходных DefaultFanTable JSON (Office/Gaming/Turbo):

- D3B07BE943C1869F619DA3BEAF0C41906BED33CF6946A95E6DFC8AA9445F7EBF
- B594F0325CCA22A9AFA7127A760163AAB19E55A638A03271FBD547EB67809934
- 28880FA37E4A30CBBD21773F5D08BDB0137085CDF0D94A3BE8186A773A08D495

Полные локальные материалы: build/research-gm6ir0c и build/gm6ir0c.
Приложение не выполняет программы, скрипты или установщики из исследуемого архива.

## Первая проверка владельцем

Запустить рабочую Windows portable, оставить штатные режимы, собрать свежие
логи и скрин FANs/SETs. Сначала проверить ProjectID/capability, два тахометра,
PWM, режимы и raw overrides. Затем по отдельности проверять Default/Max/Manual
и OEM-режимы с наблюдением физических вентиляторов. Успех записи без изменения
оборотов не объявлять рабочим управлением. Нагрузочные тесты автоматически не запускаются.

## Официальная поддержка и BIOS

BIOS-вкладка использует общий biosSupportLinks. Проверено20.10.2026:
https://www.mechrevo.com/cn/downloads?category=VGMSYq&serial=rR6Snr&display_product=rlESpy&author=277
открывает официальный каталог 极光 X с фильтром AR/PR:18 драйверов из IDC2024
(часть общих audio файлов лежит в Intel_RPL_Refresh/8L_H55). Соседняя AQ/IQ,
author278, содержит те же18 пар title/source_url. У сохранённого OEM GM6
SupportInfo указан BIOS_PROJECT_ID=IDC; это не перенос firmware compatibility.
Общий /cn/downloads без фильтров не выбирает модель; инструкции выбрать там
«极光X2024 / i7-14650HX / RTX4060» не соответствуют текущему селектору.
https://www.mechrevo.com/cn/products/ji-guang-x/downloads сейчас преимущественно
показывает поколение2026. Отдельного display_product с именем GM6IR0C нет.
HTTP200 целевой ссылки и18 результатов проверены чтением страницы/official API;
бинарные драйверы не скачивались и не запускались. Доказательства:
research/devices/gm6ir0c/support-link-20261020.txt.
Прямую ссылку на BIOS не подставлять по сходству имени GMxIR7x/GM6IR0C.
Автоматический OTA-check пока не включён. Ссылки профиля доступны и в просмотре;
снимок inventory и аппаратная identity остаются реальными.

## Intel/NVIDIA monitoring (18.10.2026)

FANs больше не использует AMD-провайдеры на Intel CPU. NVIDIA читается тем же
NVML-бэкендом, что GPU-вкладка; EC RPM/PWM сохраняют отдельный источник.
Intel i7-14650HX/Raptor Lake получает отдельный read-only путь:
GenuineIntel/family6/modelB7,BA,BF, подписанный IntelMSR.bin0.2.11,
SHA256 d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f.
Вызывается только ioctl_read_msr с allowlist1A2/1B1/606/611: TjMax, Package DTS
с valid bit31 и RAPL package energy с rollover32-bit/временем между чтениями.
Ручные ваттные EC overrides не выдаются за эту измеренную мощность. Intel MSR
записей нет. Инициализация читателя сама не устанавливает/не запускает драйвер.
Источники: https://github.com/namazso/PawnIO.Modules/blob/0.2.11/IntelMSR.p,
Intel SDM MSR definitions; для проверки семантики DTS/RAPL:
https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/master/LibreHardwareMonitorLib/Hardware/Cpu/IntelCpu.cs.
Новый путь физически на GM6IR0C не проверен; пользовательские скрины0.1.49
подтверждают существующий NVML на GPU-вкладке, не эти новые FANs данные.

## Ревизия мониторинга по журналу0.1.50

Свежие пользовательские логи подтвердили Intel DTS/RAPL, NVIDIA NVML и оба
EC тахометра. Подробная матрица и ограничения: research/devices/gm6ir0c/monitoring-audit-20261018.md.
Добавлен I801/SPD5118 monitor для RAM и NVMe SMART fallback. Их работа на GM6IR0C
ещё не подтверждена. Батарейная температура в41/41 старых samples=null;
новый лог записывает Win32 ответ BatteryTemperature и raw, не подменяет её CPU/ACPI zone.
Серийники RAM из SMBIOS не сопоставляются с SPD адресами без доказательства.

## Диагностический dump / Max probes (19.10.2026)

readDiagnosticBlock открывает только read0x000–0xFFF,≤64байт за транзакцию,
через существующие model/EC guards и gate. Это EC RAM, не прошивка flash.
measureMaximum меняет только751.6 и возвращает исходный бит после измерения
RPM. При активном OEM writer проба блокируется; служба не останавливается.
Контроль прочих fan flags и non-Boost bits751 предотвращает слепой откат
поверх внешнего режима. NVIDIA пробуется отдельно через NVML только после
подтверждённого EC cleanup. Нет fan API/исходной policy/target — нет записи;
точные getter-ошибки и результаты проб сохраняются в пакете. Статус наличия
экспорта setter не означает аппаратно подтверждённое управление.
Физический запуск новых проб на GM6IR0C требует выполнения владельцем.


## FANs как в общем интерфейсе

Windows использует общие Default / Manual / Max / Manual online и CPU=GPU.
Manual передаёт независимые16-точечные кривые штатному RamFan1.5. Manual online
использует общий fanCurveLevel с гистерезисом3°C: температуру CPU берёт из Intel
DTS, GPU — из NVIDIA NVML, раз в секунду вычисляет цель. Протокольный адаптер
записывает постоянный Duty во все слоты выбранного канала через те же native
RAM-флаги/таблицы. Регистры обратной связи0x75B/0x75C не становятся setters.
Канал без online сохраняет заводскую кривую текущего OEM-профиля. CPU=GPU задаёт
оба канала по CPU-кривой и большей из двух температур. При85°C любой из двух
датчиков online задаёт100% обоим; при потере/устаревании телеметрии завершается
с возвратом сохранённого управления. PL1/PL2/PL4 из FANs не записываются.

Редактор всех моделей/обеих ОС читает готовый fan-curves.csv одним загрузчиком.
В комплект включена целая копия предоставленного владельцем файла Wujie:
packaging/fan-curves.csv. Отдельной зашитой кривой GM6 нет. EcCurve, Manual CPU,
Manual GPU, Linked и выбранные редакторы читаются из файла, включая просмотр.
Дополнительная точка0/3°C не добавляется. Формат OEM Up[i]=next transition
обрабатывается при записи; логическая первая точка CSV остаётся в редакторе.
Прочитанная RAM — отдельное applied состояние, она не подменяет CSV.
Manual использует общую EcCurve, Manual online — сохранённые Manual CPU/GPU,
как на Wujie. Драйвер/допуск остаются GM6; неизвестные EC-карты не заимствуются.

Online хранит снимок только принадлежащих ему таблиц/масок и сверяет ожидаемые
байты перед каждым изменением и возвратом. При исходном неактивном RAM возвращает
флаги без очистки таблиц нулями. Native Manual/Default/Max завершают online через
общий typed gate. При несовпадении/ошибке readback старый снимок не навязывается;
доступен явный Default. Проба Max приостанавливает online на время30с и возврата,
сохраняя intent; обычные EC-чтения не останавливают защитный опрос. Поток online
живёт в общем контроллере, поэтому смена вкладки/предпросмотр не прерывает его.
При выходе только online возвращает своё исходное состояние; автономный Manual
по-прежнему сохраняется в прошивке. OEM-службы автоматически не останавливаются.

Linux получает ту же версию/общие элементы UI, но EC-кнопки GM6IR0C остаются
недоступны до появления подтверждённого собственного транспорта.

## RGB SingleZone и Цикл6

Windows: capability Backlight включена по активному OEM SingleZone и RGB bit
0x766.2. Gm6Controller использует общий KeyboardBacklight core под тем же gate,
точной SMBIOS/EC проверкой и запретом активного GCUService/AiStoneService.
Общая карточка управляет яркостью0–4, моно/RGB, штатной пульсацией и радугой.
Canonical UI RGB converter — чистый `gm6::nativeKeyboardColorLevels` из
protocols/uniwill/gm6ir0c/KeyboardColorCodec.h. Он повторяет реальный активный
SetRGBColor:29 palette recognition RGB, base/min1 и9 special cases, а не
metadata R_Level из COLOR_CELL. Fixed/custom/Cycle используют одну конверсию;
белый255/255/255 ->50/40/40. Сохранённые INI/actual EC RGB уже raw0..50 и
повторно через UI converter не проходят. Цветовые подписи четырёх
pulse-presets GM6 физически не сверены: режим доступен, палитра цвета пульсации
отключена, без подстановки порядка X5.

«Цикл6» — общий программный KeyboardColorCycle для Wujie/GM6. Шесть цветовых
ячеек редактируются существующим color chooser; интервал1–3600с — тем же numeric
компонентом. Параметры — keyboard-cycle.ini, startup autoplay цикла отсутствует. Таймер
не зависит от вкладки, ждёт DeviceOperation, сверяет RGB/effect перед шагом
и прекращается при внешнем изменении/ошибке. Fn-яркость не входит во владение.
При выходе таймер прекращается, остаётся последний статический цвет; старые
EC snapshots не восстанавливаются. Новый эффект останавливает цикл.
Яркость/эффект после явного действия сохраняются в gm6ir0c-device-settings.ini,
отдельно от Wujie. Чтение/preview/Fn-OSD настройки не применяют.

Статическая конфигурация GM6 теперь имеет typed lifecycle очередь
`restoreKeyboard(reason)` для startup/power_resume/system_sleep_clock и
`suspendKeyboard()` без аппаратных записей. MainWindow передаёт только события
жизненного цикла; обычный polling, открытие вкладки и idle её не ставят.
Обрабатывает существующий worker, отдельного restore/watchdog потока нет.
Восстанавливаются только явно сохранённые KeyboardLevel/KeyboardEffect0..2,
полная raw RGB тройка и PulseColor; без них нет подстановок/заводских fallback.
Неполная тройка, неизвестное/противоречивое actual состояние, неподтверждённая
identity/capability и занятый OEM завершают операцию без захвата управления.
Для смены rainbow/pulse на Mono нужен явный сохранённый RGB, а не guessed цвет.
Нет изменения FANs/APU/батареи и записи настроек из restore.

Каждая попытка получает Background DeviceOperation, KeyboardLightingPause,
EC transaction и fresh exact guards. При no-op/нет сохранённых клавишных
настроек всё равно читается actual и только он публикуется через pause.complete.
Уже работающий цикл может rebased продолжить собственный intent; новый цикл
не запускается. Доказанный transient pre-write failure может defer scope:
до5 попыток в30с с первой admission. Identity/OEM/config/state errors terminal;
после входа в возможный apply/write/strobe повторов нет, даже после rollback.
Тайм-аут после defer завершается как unknown, не оставляет цикл висеть paused.
Новый явный user command отменяет старую restore очередь; cycle ticks и
idle power-disable-only команды её не отменяют. Idle не меняет saved brightness.
Во время suspended модель не принимает keyboard writes; root передаёт общий
power epoch. Временный identity mismatch во время suspended/pending всё равно
запрещает writer, но не инвалидирует навсегда общий EC из-за wake наблюдения.
Статус/ожидание/suspend доступны в Gm6State и backlight_restore log.

Команды используют masked read-modify-write, финальный readback и ограниченный
откат собственных значений.0x7C5.7 FANs и соседние USB/lightbar биты0x767
сохраняются. АвтоВыкл после выхода пока недоступен: OEM KeyboardCloseTimer
программный и не доказывает аппаратный timeout. Новые аппаратные пробы не
запускались. Доказательства: research/devices/gm6ir0c/backlight-research-20261019.txt.

Дамп по явному запросу сначала вызывает тот же Default, останавливая online,
затем читает EC. При этом в EC не записываются ни CSV, ни factoryCurves.
fan-tables-default.json содержит исходные48байт на канал с null для непрочитанного
и отдельно OEM Office/Gaming/Turbo references из пользовательского архива.
При отключённом commit содержимое RAM не равно внутренней таблице прошивки:
дамп RAM не читает EC flash, и подтверждения экспорта внутренней таблицы нет.
Max30с запускается после снимка/первой телеметрии; cleanup возвращает Default.

## Проверочная процентная зарядка20.10.2026

Явно заказан опыт владельца: стандартные100/95/85% задают typed upper/restart
100/80,95/80,85/80 через отдельный NativeBatteryController experimental API.
Это не соответствие named protection0/1/2 и не Wujie platform4/7.
7D0.low7 пишется первым, затем7B9.low7; bit7 сохраняется; только общая
transaction/точная identity/Project25/RAM1.5/OEM guard; readback+owned rollback.
Нет нового INI/UEFI/WMI/автоприменения. Физическая отсечка/возобновление пока
не подтверждены. Подробный контракт BATTERY.md и OWNER-CHECK.txt в
research/devices/gm6ir0c/battery-dump-20261020.

Тот же присланный дамп подтвердил EC41цикл, design5200/full4300.
Windows100% health объясняется условием ISDB и cycles<50 в родном _BIF;
read-only дополнение хранит оба источника, не подменяет зарядкуВт рейтингом
адаптера и не использует X5 charger map. Ни UI-карточки, ни скролл не изменены.

Порт20.10.2026: владелец подтвердил круглый DC. Exact MyEcCtrl имеет masks
7CC&1/7CC&36; в присланном дампе raw7CC=0 приAC, поэтому отсутствие flags
не трактуется как отсутствие сети. Положительный read-only EC порт приоритетен;
при unknown/fresh WindowsAC показывается тип «Разъём питания: DC (круглый
разъём)», не EC-подтверждение активности. Native Type-C без номера, его рейтинг
GM6 неизвестен и не считывается по карте HYDROC. Геометрия карточки прежняя.

## EC-карточка батареи и Intel CPU, 21.10.2026

По трём состояниям из `logs_GM6IR0C (4).zip` подключена отдельная read-only
телеметрия `readGm6EcBatteryOnly`: точная SMBIOS, Project25/RAM1.5, два одинаковых
полных набора, повтор identity и общий межпроцессный transaction. Чистый decoder
и каталог — `src/protocols/uniwill/gm6ir0c/BatteryDecode.h`. Это общая карточка
SETs с63 строками GM6 вместо103 Wujie; высота берётся из фактического состава.
FANs supplement и EC-карточка используют один snapshot/cache; Windows-карточка
сохраняет чистый OS источник. Все поля/биты идут в общий журнал изменений/SOC/10с.
Температура не выдумывается из чужого4A2, ratingTypeC не получает чужие единицы,
порог0/0 не превращается в100%. ISDBbit7 остаётся с неподтверждённым смыслом.
В дампах42 цикла,4300/5200мА·ч=82,7%; Windows100% объяснены родным _BIF.

Общая карточка мощности показывает фактический CPU Package RAPL, отдельно от
EC-запросов PL1/PL2/PL4. Первая кнопка «Авто» возвращает только три CPU override
в0 через существующий guarded writer/readback/owned rollback без FANs/mode751.
Свежие OEM пресеты читаются из EC, архивные числа не прошиты fallback.
Общая FANs GPU-карточка выбирает максимальную валидную доступную загрузку,
а не адаптер вывода монитора; физический EC fan2 остаётся тем же каналом.
Linux использует те же данные модели/геометрию, EC writer без транспорта не включён.
Проверены offline fixtures и паритет обеих ОС; аппаратные команды здесь не запускались.

Подробности: `research/gm6ir0c-logs4-20261021/REPORT.md` и `BATTERY.md`.

# Физическая кнопка штатных профилей, 21.10.2026

Отдельная команда physical mode button176/0xB0 использует исходные backend
имена **Office → Gaming → Turbo → Custom → Office**. Описание владельца
«тихий/баланс/турбо/кастом» не заменяет канонические имена исходного кода.
Она не вызывает CPU-only SETs API и не превращает его кнопки в системные.

Общий OemEventMonitor допускает AcpiTest_EventULong для exact GM6. После
успешной подписки callback только ставит APP-presence init в существующий
controller worker. Под DeviceOperation, exact Project25/RAM1.5/OEM guards
выполняется только fresh masked741.0=1 с readback. Это соответствует OEM
App.Init/ModernOn; сам observer/OSD EC не читает/пишет. При resume повторяется
presence init для уже подтверждённой подписки. Батарея, профиль, RAM/FANs,
CPU/GPU при этой инициализации не меняются. На выходе APP не сбрасывается.
Только pre-write transient сбой допускает до пяти попыток в30с; uncertain
write не повторяется автоматически. Native admission отдельно блокирует
подтверждённый в exact архиве процесс GCUBridge.exe, а также существующие
GCUService/AiStone; общий battery owner helper не меняется.

Каждое физическое176 ставит одно нажатие в FIFO32 existing watchOnline worker;
применение сериализовано общим gate/DeviceOperation, между командами200мс.
Занятая очередь ждёт до10с, не замораживая секундный online cooling poll;
истёкшие/сон/diagnostic/shutdown события отменяются. После сна/закрытия старый
power epoch не может продолжить native записи/rollback. --no-ec не создаёт
writer. Подписка и наличие APP не считаются доказательством firmware consumer:
настоящая физическая кнопка и цветовые реакции ещё не измерены новой версией.

Текущий индекс свежо декодируется из751/726.7/727.6/7C6.2. Смешанные флаги
не угадываются. Office/Gaming/Turbo используют exact OEM factory curves с
FanControlRespective=false, маску751.B0 A0/00/10, снимают FanBoost и Custom
726.7/CustomerModeLight727.6, устанавливают RAM commit. Их PL overrides
783/784/785 обнуляются, как OEM SetUserProfile для non-Custom. Low751 и соседние
биты727/726/7C5/7C6/741 сохраняются. GPU/NVRAM/voltage/TCC/memory/батарея
не переносятся из комплексного OEM метода. Поэтому это не обещание полного
равенства всех power-policy побочных действий OEM.

Custom возвращает собственный явный runtime Manual/online curve intent и
свежо захваченный split/PL при выходе из него. Constant-duty online RAM не
подменяет температурную кривую. Без session intent берётся общий сохранённый
fan-curves.csv EcCurve, без перезаписи CSV. CPU: session PL, затем существующий
gm6ir0c-power.txt, иначе fresh Gaming PL defaults (OEM initial Mode4 policy),
без архивного ваттного fallback. Ненулевой Custom PL требует AC и известного
PL4 масштаба. Отсутствующий/невалидный CSV или draft означает явную ошибку,
а не произвольную кривую. OEM Custom table default вариативен: initial Refresh
копирует Gaming, RestoreDefault копирует Turbo; наши явные ручные данные
сохраняются отдельным источником. Никаких личных10/10/10 из OEM архива.

Явный native переход завершает предыдущий online через существующий checked
owned restore. Далее общий Journal выполняет masked writes, полную проверку
активируемых RAM-байтов/PL/режима/индикаторного флага/commit/APP и guarded
rollback только собственных совпадающих полей. Ошибка после остановки online
не обещает возобновления старого host loop. Неудачный rollback требует явного
Default/нового подтверждённого действия, без автоматической борьбы с EC.

Общий OSD показывает каноническое имя только после readback. State/log отдельно
содержат requested/observed/readback; LED цвет, реальное PWM/CPU enforcement
не объявляются подтверждёнными. FANs показывает non-Custom native RAM как
штатное управление, а не пользовательский Manual; CSV editor не перезаписывается.
Linux получает общую чистую модель имён/флагов, но новый EC writer не включён.
