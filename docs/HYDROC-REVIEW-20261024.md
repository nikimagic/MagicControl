# HYDROC-16 G2: ревизия поддержки,24.10.2026

Последующая реализация по поручению владельца: [общая карточка зон и guarded
HYDROC FAN/PL/ITE](LIGHTING-ZONES.md). Ниже сохранён исходный исторический срез
0.1.172; его утверждения о выключенных writers относятся к этому срезу.

## Короткий итог

HYDROC уже имеет подтверждённое чтение EC и нескольких датчиков. Полного обычного
управления FANs/CPU PL/подсветкой в MagicControl у него **нет**. Батарейные обработчики
подключены, но процентная отсечка/возобновление физически не подтверждены.

Главные уточнения после повторной сверки кода, OEM и новых исследований:

1. В собственном дампе **платформа7C3=13 (0D), ROM770=FF**. Это не GM6-контекст4/7.
   Нынешний guard правильно запрещает применение4/7-политики при исходном13.
2. Родной FAN-manager — **RAM Fan1.5** по собственному OEM и значениям дампа.
   Одновременно есть поддержка liquid cooling; подключённость и capability — разные биты.
3. Обычные FAN-кнопки выключены, **но активная INFO-диагностика уже имеет Default
   и factory-mailbox путь с записями**. Старое описание «весь HYDROC-дамп read-only» устарело.
4. В снимке **727.7=1**, OEM называет это CPUPL4DoubleFlag. Переносить PL4 от GM6
   как обычный байт ватт без полного разбора этого варианта нельзя.
5. Подсветка требует своего **ITE HID** пути. EC SingleZone fallback в дампе не заявлен.
6. Полный RAM-снимок уже есть. Для следующего шага прежде всего нужны **HID metadata
   и свежая пассивная телеметрия**, а не ещё один такой же общий архив.

Срез кода: `461dde5`, обе рабочие portable0.1.172. Эта ревизия — чтение существующих
файлов и документация: GUI/CLI/OEM/драйверы не запускались, EC/HID/SMBus/MSR не открывались,
новые продуктовые тесты и аппаратные пробы не выполнялись. HYDROC capabilities не расширялись.

## 1. Что именно проверено

- Исходный `Users_logs/logs_HYDROC-16.zip`, не пересказ его имён:
  SHA256 `ca5dbd707263bb057236f6db10670c26df40c3ff60e74e5a51d6d09564c33daf`,26members.
- Его EC:4096байт, маска4096/4096valid; MagicControl **0.1.69**,19.10.2026,
  около7секунд наблюдения. Это старый опыт, **не запуск0.1.172**.
- Собственный OEM **ControlCenter5.23.51.56 Eluktronics V4 / GCUService1.0.2.70**,
  восстановленные тела из `recovered-20261019/readable-csharp`.
- Текущие Profile/Diagnostics, NativeBattery, транспортные allowlist,
  BatteryEnforcement, diagnostic Default/factory, Intel readers и HID-classifier.
- Выбранные новые отчёты по platform/ROM/counter-paths доPHASE163. Они описывают
  точные свои payload; не подменяют отсутствующий исполняемый EC HYDROC ELU11.

Воспроизведение: [offline-анализ](../research/hydroc-audit-20261024/analyze.py).
[EVIDENCE.json](../research/hydroc-audit-20261024/EVIDENCE.json) сохраняет SHA исходников,
всех members архива, сырые поля и69текстовых ссылок на выбранные OEM-методы.
Это индекс доказательств, **не доказательство полного графа вызовов всего OEM**.

## 2. Идентичность и разные пространства чисел

| Признак | Значение старого дампа | Значение для вывода |
|---|---|---|
|SMBIOS|Eluktronics Inc. / HYDROC-16 / HYDROC-16 G2|Точный профиль, не маркетинговый аналог GM6|
|BIOS / EC SMBIOS|N.1.13ELU11 /1.12|EC1.12 не делает его тождественным GM6EC1.12|
|CPU/GPU|Ultra9 275HX, CPUID6/C6/2; RTX5090 Laptop|Intel DTS/RAPL и NVIDIA — отдельные API, не FAN-протокол|
|740 EC Project|1A=26|Допуск RAM/battery, не platform и не BIOS enum|
|74C|C6|Свой сырой BIOS/SKU-контекст; не присваивать ему имя платформы по одному числу|
|7C3 /770|0D=13 /FF|Своя исходная платформа; прямыеP13 и ROM/OR-предикаты различать|
|78E|FC; bit6=1|RAM1.5 capability|
|7C8 /7C7|60 /0C|LC capability7C8.5 установлена; connection-bit7C7.1 снят|
|73C|18|Подтип клавиатуры; не USB PID/firmware version и не platform24|
|766|93; bit2=0|Нет заявленного EC SingleZone fallback в этом снимке|
|7A6|08; policy-mask30=00|Native Performance; соседнийbit3 сохраняется|

**Граница текущего допуска:** `verifyHydrocDriver` проверяет точный SMBIOS и
установленный SYS; последующие guards —Project26/RAM1.5/OEM/LC. Отдельного
строгого сравнения BIOSVersion с N.1.13ELU11 в этом HYDROC guard нет.
Это не гарантия всех будущих BIOS этой платы. Перед расширением writers нужно
явно определить допущенные ревизии/признаки; существующие функции здесь не перенастраивались.

## 3. Матрица функций

| Подсистема | Что подтверждено данными/своим OEM | Что сейчас подключено | Что остаётся сделать |
|---|---|---|---|
|EC-транспорт|Полный непустой ответ4096/4096, stable Project/SKU/capability; installed SYS совпал|Read-only RAM-session с отдельным exact guard|Новый транспорт не нужен; не ослаблять ABI/hash/validity|
|CPU|DTS55–62°C, RAPL≈15.99–23.24Вт в старом опыте|Общий Intel reader допускает C5/C6 и valid bits/units|Свежий длительный пассивный лог, не проба PL|
|DDR5/SSD|SPD5118 46/45.75°C; SMART≈35.85/38.85°C|Общие I801/SPD5118 и storage providers|Проверить текущую версию по свежим ответам, не по наличию BIN|
|GPU|NVML RTX5090≈45–46°C и12.889–17.635Вт; IGCL legacy работает|Общие NVIDIA/IGCL readers; UI по общей политике адаптеров|Не назначать NVAPI channel2 VRAM/hotspot без доказательства; iGPU power-domain не подтверждён|
|PCH|В0.1.69 module-load отказал, температураnull|Нынешний общий loader уже пишет module/stage/HRESULT/Win32|Нужен свежий пассивный лог; старые7секунд не различают неподдержанный PCH и отказ загрузки|
|FANs monitoring|Свой FanInfo задаёт CPU464:465, GPU46C:46B, duty75B/75C÷2|В обычной FANs странице HYDROC RPM/PWM ещё не подключены|Подготовить read-only адаптер по этим адресам с согласованными чтениями; не включать writer вместе с reader|
|FANs управление|Свой MyFanCtrl выбирает RamFan1p5 при данных дампа|FAN capability=false; в gmRamFanProfile HYDROC отсутствует|Отдельный HYDROC admission/LC ownership, OEM defaults и ограниченное аппаратное подтверждение|
|Factory export|Родной mailboxF5D/F5E/F5F восстановлен|Уже есть узкий активный diagnostic Default/factory путь|Это **не чтение flash и не пассивный сбор**; подробнее раздел4|
|CPU PL|Свой OEM setters783/784/785;727.7 double flag|PowerLimits=false, Gm6Controller сюда не направляется|Проследить собственный double-codec/границы/preset и CPU-only путь; не назначать GM6 limits|
|Native battery policy|SetHealthProtectionHigh/Middle/Low используют7A6.4–5|Общий NativeBatteryController, saved explicit policy/restore|Не объявлять Performance/Balance/Healthy процентами; физический результат не измерен|
|Процентные upper/lower|В OEM есть private7B9/7D0 setters; полноценное исполнение start/end не установлено|Экспериментальный общий threshold API, readback/rollback|Нет доказательства stop/restart, особенно native95/90; разобрать настоящий consumer точного ELU EC|
|Platform/ROM|Новый общий policy-код умеет4/FF↔7/04 с machine-bound ownership|HYDROC handler существует, **но при исходномP13 guard отказывает**|Сохранять13/FF. Не обходить guard ради процентной кнопки|
|Подсветка|Свой LM_Manager сначала выбирает ITE HID; команды/калибровки разобраны|Backlight=false; HID helper только annotations, permission=false|Нужен actual HID descriptor/firmware/type; EC fallback не включать|
|Автогашение|В OEM есть host KeyboardCloseTimer с keyboard+mouse activity|Без HID light-off/on команд собственного HYDROC backend нет|Не подменять его welcome1A или чужим741-bit экспериментом|
|Клавиши/кнопка профилей|Общие системные KEYs независимы от EC; OEM mode-chain есть в собственном пакете|Микрофон/OS-switches — общие; специальная AcpiTest cycle-подписка GM6 не включает HYDROC|Нужны свой event-code/cycle/LC+profile ownership; не обещать рабочую физическую кнопку|
|Linux|Профиль/общие страницы и системные providers|Windows EC transport отсутствует; статус «EC не подключён»|Отдельный kernel/API transport; не подменять его 256байт ec_sys|

Старый дамп даёт арифметические кандидаты **CPU2708 /GPU2645 RPM**, duty49.5/49.5%
по собственному FanInfo. Это не серия измерений оборотов, не калибровка тахометров
и не наблюдение реакции на команду. Возможность вычитать эти поля не разрешает FAN writer.

## 4. Диагностика: существенное расхождение старого README с кодом

Текущая цепочка:

`INFO/WithFanProbes → preparePlatformFanDefault → prepareHydrocDiagnosticDefault`

затем при подтверждённом baseline и успешном первом проходе датчиков:

`capturePlatformFactoryTables → exportHydrocFactoryTables → exportRamFanFactory`.

Это присутствует в текущих
[DiagnosticDumpWindows](../src/platform/windows/DiagnosticDumpWindows.cpp),
[общем оркестраторе](../src/platform/common/DiagnosticDump.cpp),
[HYDROC factory-adapter](../src/platform/windows/RamFanFactoryWindows.cpp),
[общем RAM1.5 exporter](../src/protocols/uniwill/RamFanFactory.cpp).

- Diagnostic Default снимает commit7C6.2, custom726.7, indicator727.6, Boost751.6.
  Это реальные masked writes, хотя обычной FANs-кнопки нет.
- Export временно меняет APP741.0 и mailboxF5F/F5D/F5E, запрашивая Gaming/Office/Turbo.
  `F00–F5F` — RAM-зеркало, а не firmware image. Пересечение последних duty-байтов
  с mailbox нельзя игнорировать.
- Вход проверяет pinned драйвер/SMBIOS/Project/RAM1.5 и отсутствие OEM-владельца.
  ЕслиLC capability установлена и connection7C7.1=1 — отказ. Одна capability
  не является признаком подключённого охлаждения.
- Cleanup не обещает вернуть произвольный прежний Manual/LC snapshot: RAM-зеркало
  оставляется неактивным, APP/Default подтверждаются по контракту exporter.
- Отдельные Max EC/NVML пробы HYDROC не становятся разрешёнными из факта существования
  factory-adapter. Обычные FANs/PL/RGB capabilities остаются выключенными.
- В `DiagnosticDumpMode::ReadOnly` и Default, и factory-export пропускаются.
  В этой задаче **ни одна из этих функций не вызывалась**.

Таким образом, писать «HYDROC диагностический backend только читает» корректно
для RAM-reader; писать «любой INFO-дамп HYDROC не содержит записей» — уже неверно.
Старый архив0.1.69 не проверяет новый active factory путь.

## 5. Батарея: четыре разных механизма

### Именованные режимы

Собственный BatteryProtection2:0=High/Performance,1=Middle/Balance,2=Low/Healthy;
в7A6 меняются только биты4–5. При raw08 режимHealth дал бы masked28,
а не замену всего байта20. Текущий транспорт сохраняет соседние биты.
Private percentage setters не являются доказанными callers этих режимов.
Физический эффект Healthy в точном ELU11 не установлен.

### Проценты

7B9.low7 и7D0.low7 присутствуют в восстановленном OEM, но наличие setter/UEFI-поля,
запись и readback не доказывают реальное возобновление ниже выбранного процента.
В старом снимке оба raw0; это не наблюдение настроенного80–85 или95–90.
Ранние заголовки отчётов «настоящий stop/restart» читать вместе с их оговоркой
о незавершённой цепочке. Роль нижнего здесь остаётся неизвестной.

Текущий общий `applyExperimental` при restart<0 использует
`selectedChargeRestart(id,80)` (кроме специального upper10/preserve).
Значит клик upper способен подставить сохранённый/default80 вместо живого lower;
обнаруженный на GM6 дефект нельзя считать исключительно GM6-специфичным.
Для HYDROC это **вывод по общему коду**, не новый зарегистрированный аппаратный случай.

### Переключение платформы/ROM

Threshold-transport уже расширен до разрешённых4/7 иFF/04, плюс threshold-байты.
Но `requireContext` допускает исходныеP только4/7. Для собственного снимкаP13
`enforcementWritable` не выставляется; `setEnforcement` должен завершиться отказом
до policy writes. Обходить его или сначала переводить13→4 для «починки батареи» нельзя.

Из новых firmware-разборов известно, что P иROM имеют побочных потребителей;
P7/R4 не равноP4/RFF. **Это предупреждение о границах переноса**, а не разбор
точного кода ELU11. GM6/GX/AR-ветки не присваивают HYDROC готовую семантикуP13.
В [PHASE161](../research/ec-platform-catalogue-20261023/PHASE161-GM6-P2-DR-P13-CALLERS.md)
и [PHASE163](../research/ec-platform-catalogue-20261023/PHASE163-LAST-DIRECT-ANCHORS.md)
P13 прослежен в отдельных клавиатурных ветках X6DR/HG. Это дополнительное
основание не считать его «только батарейным режимом», но не идентификация ELU11.

### Сохранение и запуск

NativeBattery восстанавливает только явные ключи из своего INI. Сейчас это не
только старый BatteryProtection: существуют ChargeAuto и ChargeRestart ветви.
НаP13 неизвестный контекст блокирует policy-ветвь; сохранённый native mode
мог быть применён до последующего отказа другого поля.
Простой upper click HYDROC не получает GM6-специфичное rememberThresholds.
Поэтому общее обещание «сохраняются все проценты» или «запуск вообще не пишет» неверно.
Разбор этой задачи ничего не запускал и не переписывал пользовательские настройки.

## 6. Подсветка и idle timeout

Собственный OEM различает минимум четыре уровня: coarse support-string,
LM enum/transport, ITE HID descriptor+FW80+EC73C и класс HIDKeyboardFactory.
Число1 в одном из них не доказывает SingleZone в другом.

В старом архиве нет FeatureReportByteLength/UsagePage metadata и не установлен
конкретный HID instance. Это не доказательство отсутствия ITE устройства.
Нужны VID/PID,Usage/Page,реальный feature-size, выбор keyboard против lightbar,
и затем — отдельное подтверждение FW/type/calibration. USB bcdDevice не заменяет FW80.

По собственному OEM:08/09 — эффекты/яркость,80 — firmware query,88 — состояние,
14 — RGB,1A — welcome-timeout. ЯркостьUI0–4 имеет raw0/8/22/36/50.
**1A не является idle-таймером в секундах**; сохранение welcome может затронуть UEFI.
Host KeyboardCloseTimer использует keyboard+mouse activity и требует надёжного
выключения/восстановления именно своего HID состояния. Никаких HID reports здесь не отправлялось.

## 7. Мощность, LC и горячие клавиши

В собственном MyFanManager_RamFan1p5 setters7211/7217/7223 пишут783/784/785.
PL4 перед этим читает727.7; в дампе он установлен. Другие функции проверяют
минимумPL4 относительно PL1/2 с делением на2 при double-флаге.
Это требует полного tracing кодека и границ; raw-byte не равен автоматически ваттам.
Пакетный RAPL — наблюдаемая мощность, не readback заданных PL и не источник их максимумов.

Native whole-system режим включает не только PL, но и FAN/индикацию/LC-политику.
Будущая CPU-карточка должна оставаться CPU-only; нельзя вызывать целиком OEMSetUserProfile
ради копирования трёх ватт. Нужны собственные заводские тройки этого HYDROC.

LiquidCooling.SetECSupport задаёт7C7.1 из `_Connected`;7C8.5 — возможность LC,
а HWOC-биты — третье состояние. Это нельзя схлопнуть в «LC есть/нет».
Фактическая насосная/BT-периферия в старом семисекундном дампе не аттестована.
Прежде чем разрешать пользовательские кривые, нужно решить владение при подключении LC.

В нынешнем OemEventMonitor специальная AcpiTest_EventULong подписка допускает
Wujie и gmRamFanProfile, а HYDROC туда не входит. Поэтому наличие общей реализации
кнопки GM6 не делает её автоматически рабочей на HYDROC.
Нужны собственные event-code, cycle и условия владения; guessedcode176 не включается.

## 8. Что делать дальше, по приоритету

1. **P0: сохранитьP13/ROMFF и read-only режим следующего сбора.** Не просить владельца
   запускать обычный активный INFO ради HID metadata; новый RAM-дамп сам по себе не главный пробел.
2. **P1: подключать read-only RPM/PWM отдельно от FAN writer.** Для адресов/формата
   есть собственный FanInfo и валидный RAM. Нужны stable-read и текущая телеметрия,
   без factory mailbox, без Default/Max и без включения FAN capability вместе с reader.
3. **P1: HID metadata/свежие module-error stages.** Сначала сведения ОС, без80/08/09/14/1A;
   для протокольного запросаFW — отдельно обоснованный следующий шаг.
4. **P2: FANs.** Сверить свой RAM1.5 кодек/LC ownership/factory-result, точный admission
   BIOS/EC-ревизии, собственные настройки/rollback. Только затем отдельно подтверждать исполнение.
5. **P2: CPU PL.** Проследить double-флаг и диапазоны/родные defaults, не наследовать GM6.
6. **P2: батарея.** Отдельно исправлять передачу живого lower и искать исполнение
   nativeHealthy/start. Для окончательного static proof нужен exact исполняемый EC ELU11;
   SRAM и одно совпадение номера1.12 не заменяют его.

Ни один пункт этого плана не был аппаратно выполнен данной ревизией.
Она не является разрешением автоматически включить HYDROC FANs/PL/HID/MUX writers.

## 9. Как читать старые документы после этой ревизии

- [README модели](../src/devices/eluktronics/hydroc-16-g2/README.md) сохраняет историю:
  утверждения о строго двух threshold-полях/полностью пассивном дампе относятся
  к прежним реализациям, а не к полному текущему call path.
- [CONTROL-REVIEW19.10](../research/devices/hydroc-16-g2/oem-5.23.51.56/CONTROL-REVIEW-20261019.txt)
  остаётся основанием OEM-методов, но не доказывает их сегодняшнее подключение/физический эффект.
- [Подсветка20.10](../research/analysis-20261020/keyboard-backlight-agents/hydroc.txt)
  уточняет host idle против welcome1A и границы HID-классификации.
- [Новая сводкаP/ROM](../research/ec-platform-catalogue-20261023/PLATFORMS-5-6-7.md)
  полезна для запрета неверного переноса; exactHYDROC firmware в ней не появился.
- [Матрица16моделей](DEVICE-SUPPORT-MATRIX.md) получает ссылку на эту ревизию;
  статус HYDROC остаётся частичным, не «полностью поддерживается».
