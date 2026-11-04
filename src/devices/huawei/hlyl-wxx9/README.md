# HUAWEI / Honor MagicBook Pro — HLYL-WXX9

## Штатные графики FANs,01.11.2026

NativeDefaultFan теперь содержит read-only RPM-представление Honor BIOS1.09.
Таблицы взяты из встроенного EC-образа точного свежего дампа; SHA и разбор:
research/honor-fan-curves-20261101. Это документированная база прошивки,
а не новое чтение работающего EC flash и не редактируемая Manual-кривая.

Существующий HlylFanController.read в той же guarded I2EC-сессии получает
TUBO315.6, нормальный Default042C,8 температур/ступеней, общую ступень536,
запросы534/53A и обратную связь03CC/03CE. Смена контекста/ступени или torn
16-bit read даёт unavailable. Служебные scratch pointers6A6/6A9 не являются
постоянной identity: их не используют как idle-time guard. BIOS/IT8987,
S0 и period200 сохраняют прежние границы транспорта; записей payload нет.

Ядро выбирает датчик с максимальной фактической ступенью; равенство разрешает
предпочтение TSLO, затем CPU. Все8 входов, включая unnamed0321/0322, остаются
в DTO/ResourceCatalog с порогами разгона/снижения. Обе FAN-кривые используют
этот общий управляющий датчик, свои RPM-значения и отдельные запрос/факт.
Ступень запроса может отставать от максимального требования из-за slew EC.
Никакой подстановки GPU/Tctl и сравнения температур разных датчиков нет.

Windows FAN monitoring переносит DTO в accepted ResourceState; обе GUI
переиспользуют прежний график с RPM-шкалой. Native Manual/Max/default setters,
PWM controls, watchdog, ownership и сохранённые настройки не менялись.
Manual сразу отключает factory-view; transient failed observation использует
прежний10-секундный display cache с исходным временем и stale-подписью.
Linux без подтверждённого HLYL I2EC reader не фабрикует native observations.
Нового timer/worker/GUI reader, fan write, mode switch или startup replay нет.

## Срез0.1.241,27.10.2026

Honor — собственный Windows HWMI/I2EC/FAN/SMU backend, не battery-only профиль
и не Uniwill map. BIOS1.09 InsydeH2O читается штатным H2OFFT6.76/SYS3.0.0.3
через fixed -g; интеграция0.1.239 подтвердила два одинаковых16МиБ ROM и3FV.
Точные pins: src/platform/windows/runtime/InsydeH2offt/PROVENANCE.json; evidence:
research/insyde-update-20261027/VERIFICATION-239.json. Полнота физическогоSPI
и liveEC flash отдельно не подтверждены. Старый reader6.25 — история0.1.238.
Локальные сборки не доказывают удалённую запущенную версию; Linux использует
только реально доступные sysfs операции, без переноса Windows writer.

24.10.2026: UI snapshots use shared core DisplayContinuity on Windows/GTK.
Transient HWMI/sysfs/EC observation failures retain past numeric/selection data;
fresh connected/writable and command readbacks never come from that cache.
No Uniwill RGB registers, new writers or changes to PNPCFG finish/rollback.

Общая карточка подсветки использует единые кнопки яркости. Профиль
задаёт максимум2: OFF/LOW/HIGH (0/1/2). Это только диапазон UI; прежние
typed setter, guards, selected/actual PWM и native idle semantics сохранены.

## Подсветка: исправления20.10.2026

Белые OFF/LOW/HIGH остаются native selected уровнями0/1/2. Фактический PWM2
отделён от выбора: при period200 это0/50/100 raw, а native idle может пройти
HIGH100/80/60/40/20/0 или LOW50/30/10/0 без изменения selected/shadow.
Read-only classifier принимает fade только при прежних exact BIOS/IT8987/
period/config/power guards, совпадающем shadow, ненулевом seconds*10 timeout,
elapsed>=timeout, рассмотренной duty sequence и fade window<=timeout+10 ticks.
Native idle OFF требует тот же контекст и duty0. Иное расхождение — unknown,
без нового raw writer или подмены показаний. Сигнатура resume0/shadowHIGH/duty0
остаётся узким исключением; её валидность больше не зависит от цели OFF/LOW/HIGH.

`HlylBacklightState` и общий OEM snapshot сохраняют actualDuty/period/status.
Явная кнопка подтверждает выбранную политику; уже идущий firmware idle может
снова уменьшить PWM. Программа не обнуляет activity counter и не имитирует
клавиатурные события ради света. RGB/цветовой цикл на Honor не включаются.

Портовые читатели/brightness setter используют checked `finish()` до возврата
результата и до success log, включая no-op. Failed finish не вызывает новых
target writes; destructor лишь повторяет безопасную попытку выхода. Fresh
masked RMW03FC[C0] на уровне транспорта сохраняет low6 также при rollback.
Availability учитывает query-only статус HuaweiKeyboardLight; неизвестная или
активная legacy service блокирует brightness запись без её остановки.

`readHlylKeyboardDiagnosticJson()` — fixed read-only context для parent dump,
не SFR sweep и не killable worker: selected/shadow, PWM1804/period, timer/elapsed,
guards/raw/errors/timestamps, service state и GKBT после checked PNPCFG exit.
Сам RAM0..FFF этих данных PWM1804 не содержит. Значения электрического пина
не объявляются оптическим измерением света.

Таймаут GKBT1206/SKBT1106 остаётся в секундах0..3600, rawEC=seconds*10.
Единый `protocols/huawei/KeyboardTimeout.h` обслуживает HWMI и реально существующий
Linux sysfs: два совпадающих чтения, bounded verify, rollback только своего
значения, явное unknown при отказе/чужой смене. Linux upstream сам не обещает
LED/timeout attributes; без подходящего драйвера/fork элементы unavailable.
Новых аппаратных проверок в этой правке не выполнялось; источники/ограничения:
`research/analysis-20261020/keyboard-backlight-agents/honor.txt`, исправления:
`research/fixes/keyboard-20261020/honor.txt`.

## Исправления статической ревизии19.10.2026

Native charge0/0 распознаётся как отключённая политика, не100%. Два совпадающих
чтения допускают выход из этого состояния по явному95/100; raw0/0 разрешён лишь
как ранее прочитанный rollback baseline, не новый пресет UI. Windows/Linux
проверяют откат чтением, сообщают неизвестное состояние отдельно и не заменяют
значения, не принадлежащие текущей операции. Общая семантика пары находится в
`src/protocols/huawei/BatteryThresholds.h`.

HWMI ошибки сохраняют opcode/аргументы/status/длину/raw header ответа; отдельные
getters не исчезают в пустых catch. Шесть GTMP rows сохраняются и при отказе
всей сессии. Ошибка battery-only GTMP14 записывается и передаётся вызывающему коду.
Наш HWMI mutex общий для Windows sessions, но не исключает сторонний PC Manager.

`HlylAutoResult` различает Applied/AlreadyAuto/OwnershipLost. Watchdog/cleanup
не объявляют восстановление Auto при чужом manual. `measureMaximum(stop_token)`
резервирует общий DeviceOperation и использует общий30-секундный FanMaximum;
требует Default, свежую CPU<80°C и проверенный PWM100/100. По отмене/ошибке
всегда вызывается ownership-aware cleanup; чужие targets не перезаписываются.
КомандаPWM, измеренная реакция RPM и стабилизация — разные результаты.

Resume HIGH проверяет только принадлежащие яркости03FC[7:6], сохраняя соседние
биты. BIOS/IT8987/PWM/power guards не ослаблены. Эти изменения проверены лишь
чтением исходников; новые аппаратные пробы/сборки в этой ревизии не запускались.
Подробности: `research/fixes/models-20261019/honor.txt`.

## Исследование OEM-драйверов19.10.2026

Разобраны установленные PC Manager11.1.6.31 / WmiUtil11.0.0.0,
HardwareHal, HwDriverAdapter и MateBookService. Штатный путь датчиков —
Microsoft WmiAcpi + HWMI, отдельный Huawei SYS для него не требуется.
Найденный в EXE HwOs2EcX64 используется разобранными функциями BIOS Logo,
на машине его драйвер не найден; не подменять им PawnIO.
Чтением подтверждены GTMP0/5/7/8/11/14 (CPU/TSLO/TNTC/CNTC/DNTC/батарея),
GFNS0/1 и GCVA0x20/0x30. Не назначать неизвестным термоканалам названия RAM/GPU.
BIOS → «Снять дамп» подключён к PawnIO/LpcIO/I2EC: диагностическое окно RAM
0x0000–0x0FFF,4096 байт, с маской валидности и ошибками. Это не образ прошивки
и не sweep SFR/flash. Сессия проверяет BIOS1.09/IT8987 и освобождает ISA mutex
и PNPCFG после каждого16-байтного блока; модуль PawnIO остаётся закреплённым.
GTMP0/5/7/8/11/14 сохраняются в общем MonitoringSnapshot.additionalSensors
и sensors.jsonl под OEM-именами, включая ошибку вместо нулевой температуры.
Battery-only путь тоже использует свежий GTMP14, когда Windows его не сообщает.
Доказательства, хэши, декомпиляция и применимость найденных EC-драйверов:
`research/devices/hlyl-wxx9/honor-20261019/EC-OEM-RESEARCH.md`.

## Текущий итог на18.10.2026

- Подсветка: native I2EC OFF/LOW/HIGH проверен, пользователь подтвердил свет.
- Зарядка: 95/100 принято на AC.70/80 при заряде>80 физически остановило
  заряд с21Вт до0; исходные70/90 восстановлены. До100 батарея ещё не доведена.
- Вентиляторы: штатный BIOS разобран, runtime ручной PWM обоих вентиляторов
  подтверждён пробой50%/10сек, RPM≈5000/3600. Native0.1.32 повторила результат
  и вернула автоматику, исходные targets и PWM0/0. В0.1.33 FANs подключён к
  отдельному HlylFanController: два фиксированных PWM, Default/Manual/Max.
  Геометрия карточек/скролла общая. Таблицы/автономные кривые MECHREVO не используются.
  Watchdog при явном manual возвращает Auto при80°C/потере телеметрии;
  выход освобождает только ручной режим, которым владел этот контроллер.
  Работа всей шкалы и визуальная проверка GUI пока не завершены.
- APU: native0.1.32 выявила коэффициент MP1/PM0.9 и неверный откат через
  эффективные PM-значения. Штатное Turbo0→1→0 вернуло31.5/37.8/54Вт.
  В0.1.33 не предполагается постоянный коэффициент: два совпадающих чтения
  всех трёх полей допускают одну коррекцию на0.9 в рамках явного действия;
  повторный readback обязан совпасть с реальными запрошенными ваттами.
  Native0.1.34 подтверждена на AC:20/25/30Вт, повторное чтение через3сек,
  точный возврат31.5/37.8/54Вт. Журнал smu-native-roundtrip-0.1.34.txt.
  При неизвестном
  коэффициенте откат не делает повторную запись вслепую. MP1 raw cap60Вт
  соответствует верхнему эффективному54Вт только при доказанном0.9.
  Это не отключение STT/термозащиты и не фоновое enforcement.
  Режим95/100 дополнительно проверен по реальному заряду4.3Вт на AC;
  исходные70/90 после проверки восстановлены.
- Linux использует лишь реально доступный sysfs. Windows I2EC и SMU setters
  не имитируются. Обе рабочие portable имеют общий номер билда.

Журналы и подробные границы: research/devices/hlyl-wxx9/honor-20261018/RESULTS-20261018.md.

## Подсветка IT8987 — проверка 18.10.2026

На заводском BIOS1.09 WMI GLIV0602 возвращает ошибку, а SLIV0702 сообщает
успех без физического изменения. Этот путь не используется для яркости.
Пользовательская HLYL-WXX9_KeyboardLight_WORKING подтвердила три круга
OFF/LOW/HIGH чтением и визуально. Native-перенос находится в BacklightWindows.cpp:
PawnIO LpcIO0.2.10, I2EC IT8987, только runtime02F4/03FC/1804.
Перед доступом проверяются точная плата/BIOS, ID/canary8987, режим SPCTRL1,
питание, guard и конфигурация PWM. Соседние биты shadow сохраняются.
Запись проверяется чтением и PWM mirror, при ошибке выполняется ограниченный откат.
Запуск/выход не меняют яркость. Flash и вентиляторы этим кодом не записываются.
Linux использует существующий sysfs, Windows I2EC туда не переносится.
Native-перенос0.1.28 проверен на самом Honor: CLI OFF→LOW→HIGH и HIGH в конце,
readback0/1/2, проверка PWM mirror и guards прошли. Журнал:
research/devices/hlyl-wxx9/honor-20261018/native-backlight-0.1.28.txt.

Уточнение18.10:03FC изменяется только по маскеC0, младшие6 бит заново читаются
непосредственно перед записью, в том числе при откате. Четыре уровня GPIO
A1/C0/E2/G1 из старой утилиты исключены из сравнения стабильности: BIOS1.09
1647C/164A3/164D9 не использует их как разрешение PWM2, а отдельные процедуры
1A12D/1A16D меняют их независимо. Их конфигурация по-прежнему проверяется.
Контроль042C.bit0/031A.bit0/0400/GPIOC6, clock gates, period и PWM mirror сохранён.

## Разбор оригинального BIOS1.09 — 18.10.2026

Получена сохранённая владельцем оригинальная16-МиБ копия, SHA256
38597994DC39ED17D57CEE8331AC107579B6FF186934CDBC873D3C5F363A806E.
Анализ офлайн, прошивка и flash-записи не выполнялись.

- SBTT1003 при входе0/100 явно записывает0/0 (DSDT). Поэтому это не полный
  заряд95/100. GBCM1603 сейчас сообщает mode2, DTTS72, пороги70/90.
  Наличие заданных порогов ещё не доказывает их выполнение в Smart Charge.
- GFNM/SFNM вызывают SMI через портB0,C1. WmiCallbackSmm GUID
  F352092C-E0B3-4237-BBE8-41110791545B имеет четыре записи dispatch-table4180:
  group6 sub1/2/7/8. Ветка3158 также обрабатывает group3/5; group4 там отсутствует.
  Нулевой ответ0C04 не разрешает запись0D04: возможен неизменённый буфер DAOU.
- EC-код в начале образа: процедура file13A50 проверяет XRAM042C.bit1 и
  передаёт min(0532,1801)→1808, min(053C,1801)→1809. Это кандидаты ручных
  target/PWM обоих вентиляторов; назначение впоследствии подтверждено пробой50%.
  В CLI oem-fans добавлено только чтение этих фиксированных адресов.
  Native setter и возврат автоматики проверены отдельно; см. текущий итог выше.
- SMU0.1.29: первое обновление таблицы вернуло ошибку, повторное чтение
  успешно: PM370003, STAPM/Slow/Fast31.5/31.5/31.5 Вт на батарее.
  Добавлены ограниченные повторы telemetry transfer10/100мс только для
  Renoir FD/FC согласно RyzenAdj request_transfer_table. Limit setters не ретраятся
  по ошибке транспорта; отдельная bounded коррекция0.9 описана выше.

## Идентификация и границы

Требуются manufacturer=HUAWEI, productName=HLYL-WXX9, baseBoard=HLYL-WXX9-PCB.
Отчёт пользователя: Ryzen5 4600H, BIOS1.09 от18.05.2022, EC1.9, Windows11.
Вариант HONOR/BIOS3.xx автоматически не допускается этим профилем.

Код реализует опубликованный OEM-интерфейс с проверкой ответов. Журналы0.1.16
с пользовательского Honor подтверждают WMI/RPM/Fn/Turbo и SMU-чтение. Readback порогов не доказывает
физическую остановку зарядки; это отдельное аппаратное подтверждение.

## Возможности

- BIOS-ссылки: поддержка https://www.honor.com/ru/support/ и точная страница
  https://www.honor.com/cn/support/products/laptops/magicbookpro2020-ryzen/.
  Автоматический MECHREVO OTA на HLYL не вызывается. Nahimic сохраняется:
  пользователь подтвердил его наличие на Honor. Windows/Linux используют общий выбор ссылок.

- Windows: ROOT\WMI / OemWMIMethod.OemWMIfun, u8Input[64] или u64Input по сигнатуре.
- Чтение RPM fan0/1, температурыCPU GTMP0 и батареи GTMP14.
- Зарядка: getter/setter start/end. Кнопки100/95/85 задают95–100/90–95/80–85. 0–100 означает отключение защиты, а не travel/full-charge; на Honor дало ошибку и 40–70.
- Fn Lock: getter1/2, setter1/2, с подтверждением чтением.
- Turbo Huawei в Windows: штатный флаг GTUB/STUB (0x0E04/0x0F04), только от сети,
  по успешному getter и readback. Это не ручное задание ватт и не гарантия мощности.
- Запись разрешается только после корректного ответа соответствующего getter.
- CPU Tctl/энергия: AMDFamily17 при установленном PawnIO. DDR5 SMBus-опрос на этой
  DDR4-плате отключён. Для CPU load есть системный fallback GetSystemTimes.
- SMU только для CPUID family17h/model60h и PM0x370000…0x370005. Неподдерживаемая
  таблица не декодируется. Читатель общий: platform/windows/pawnio/ApuPowerWindows.cpp.
- Linux: штатные sysfs charge_control_thresholds / power_supply start/end и
  fn_lock_state от huawei-wmi. Кнопки требуют читаемого значения и прав записи.

Raw EC backend и boardVerified у Huawei отсутствуют. Никаких82/72/26,0x7C3,
RAM-таблиц MECHREVO. Фиксированный Manual/Max Windows реализован отдельно;
автономные кривые EC и RGB не включены. Явный SMU-setter
реализован отдельно (см. продолжение ниже); на самом Honor пока не проверен.
RPM не преобразуются в PWM. Fan1 не объявляется физическим вентилятором iGPU.
При старте/выходе Huawei-настройки автоматически не применяются и не сохраняются
в MECHREVO device-settings.ini. Windows --no-ec отключает Huawei WMI.

## Первичные источники, просмотр17.10.2026

1. https://github.com/torvalds/linux/blob/master/drivers/platform/x86/huawei-wmi.c
   — команды зарядки0x1103/0x1003, Fn0x0604/0x0704 и ABI sysfs.
2. https://github.com/AceDroidX/HuaweiBatteryControl
   — commit1ca58e104dffb821da00763bbf449bd006d84ccf, WMI u8Input[64].
3. https://github.com/shockwave-coder/matefan
   — commit38d04965853f6c71e94c300b758539c5779c175b, HwmiCodec/discovery.md.
   Windows u8Output начинается с status0, без offset4. Измеренный профильFLMH-XX
   и его диапазон RPM не переносятся на HLYL.
4. https://github.com/Pinghigh/Magicbook-Pro-2020-R7-4800H-Hackintosh
   — commit221c2934020a6a2adf7beb0695392c3598b94280, EFI/OC/ACPI/DSDT.aml.
   Таблица HONOR/EDK2 изменена для Hackintosh: это не заводской дамп BIOS1.09.
  Разобрана iasl20260408. GBTT/SBTT, GFRS/SFRS, GFNS, GTMP, GTUB/STUB найдены;
  STUB меняет TUBO только при ACDC=1. SFND не найден.
5. https://github.com/FlyGoat/RyzenAdj
   — commit5775fc3e6dbb25c7030ee2d100a1bdd6e8bf2d0a, lib/api.c: PM37xxxx и
   offsets0x00/0x08/0x0C/0x10 для STAPM/Fast/fastPower/Slow.

Транспорт реализован самостоятельно по интерфейсам. Сторонние проекты не
запускаются и не поставляются. Сырые материалы: research/devices/hlyl-wxx9/;
не включать их в публичный релиз. Для первого запуска диагностические строки
huawei_hwmi и SETs APU показывают фактически доступные поля/формат PM.

## Продолжение исследования — 17.10.2026

Windows 0.1.16 проверена пользователем на HLYL: WMI u8Input[64], CPU40–82°C,
оба RPM, Fn Lock 0→1→0 и Turbo0→1→0. Turbo меняет Slow37.8→40.5→37.8,
STAPM31.5→36→31.5 при Fast54 Вт. PM-table0x370003. Пороги90/95 и80/85
подтверждены чтением. 0/100 дважды приводило к40/70; физическая остановка
зарядки пока не подтверждена. Исходная пара70/90; не терять её в диагностике.

Исправление 100%: штатный travel95/100 из китайского разбора PC Manager
https://blog.acedroidx.top/HuaweiBatteryControl/ (коды645F1003 vs64001003).
Порог проверяется ограниченной серией чтений, временный0/0 не принимается за
успех. При ошибке предпринимается откат с отдельным логом. Без autoreapply.
Smart Charge описан в https://github.com/qu1x/huawei-wmi: нулевой порог может
включать его, а сохранённые пороги тогда не доказывают фактическую остановку.

Новая запись SMU: только HLYL+Renoir17h/60h+известная PM37xxxx. MP1 addresses
message3B10528/response3B10564/args3B10998, команды14 STAPM/15 Fast/16 Slow
из RyzenAdj5775fc3 lib/nb_smu_ops.c иapi.c. Это НЕ PSMU3B10A20, который
выбирает ioctl_send_smu_command в модуле RyzenSMU для Renoir! Используются
ioctl_read_smu_register/ioctl_write_smu_register неизменённого подписанного
модуля0.2.11 под Global Access_PCI. Диапазоны приложения: AC STAPM/Slow/Fast
5–45/45/54, DC5–30/30/35 Вт, STAPM≤Slow≤Fast. Это ограничения приложения,
не измеренная максимальная способность ноутбука. Разовое явное применение,
проверка PM и попытка отката. На самом Honor новый SMU-setter ещё не проверен.
Пассивный опрос никогда не выставляет лимиты и не борется с прошивкой таймером.

Подсветка: HLYL reference имеет GLIV/SLIV (яркость), SKBT/GKBT (таймаут).
SKBT1106/GKBT1206 задают секунды0–3600,0=без автоотключения; запись разрешена
после корректного getter с readback. API яркости нового Honor1306/1406 нельзя
включать на старом HLYL лишь по правдоподобному ответу:1306 также встречается
как ReadMemoryTrainingData. Нужен настоящий WMAA dispatcher BIOS1.09.
Linux использует только существующие huawei::kbd_backlight brightness/max=2
и, при наличии, kbdlight_timeout. Не устанавливать драйверы и не менять права.

Вентиляторы: проверенные чтения0802 остаются. MateFan38d0496 описывает SFND1102
для FLMH-XX; sealeelike/Honor-FRI-H76-fan-controller-for-linux — EC0A18/0A19
для FRI-H76. Эти команды/регистры НЕ подтверждены на HLYL. SFNM в изменённой
DSDT HLYL делегирует SMI; сама таблица не раскрывает допустимые значения.
Не разрешать Max/Manual/Online или переносить калибровку RPM другой платы.

Tools/Collect-HonorDiagnostics.ps1 собирает штатные ACPI через Windows API и
HKLM HARDWARE ACPI (включая дополнительные SSDT, если есть), точную идентичность,
WMI ABI и фиксированный список getter-команд. Не пишет настройки/EC/SMU,
не устанавливает драйверы и не выгружает данные в интернет. Запускать на Honor;
для команды1306 сохранять сырые байты, не считать её назначение доказанным.

Дополнительные просмотренные источники:
- Wintego/Honor-PC-Helper84927d6fa7fb586f91ce9a1e09a9490da6188b78 — новая ZQC-P,
  три уровня подсветки/таймаут; не источник точных регистров HLYL.
- PawnIO.Modules24425a5299e84410751da882c121f322fb030b82 — описание IOCTL;
  поставляемый модуль по-прежнему0.2.11, его hash/provenance не меняются.
- https://www.honor.com/cn/shop/help/category-88.html — OEM MagicBook Pro2020
  Ryzen4600H/4800H,45W CPU/35W platformTDP. Не переносить сведения с Intel2020.

Цель исследования остаётся открытой до штатного дампа и аппаратного readback
новых функций. Не объявлять компиляцию подтверждением работы на Honor.


## Исправления владения после ревизии19.10.2026

Watchdog и выход снимают manual bit только при совпадении обоих текущих targets
с установленными данным контроллером. Проверка идёт до записи, под ISA/I2EC mutex.
Внешнее изменение завершает наше владение без сброса чужого режима. Явный Default
запрашивает Auto независимо от текущего владельца. В каталоге профиля перечислены
реализованные typed FANs/PowerLimits/Backlight; отсутствие raw EC backend по-прежнему
не допускает Uniwill/X5 handlers. Runtime getter/BIOS/ОС дополнительно ограничивают
доступность каждого элемента.

## Общее журналирование действий24.10.2026

Фабрики Windows/Linux используют общий typed LaptopControl audit decorator:
requested level/timeout/Fn/turbo/charge, уже полученные snapshots и ошибки.
HWMI/sysfs/I2EC сохраняют существующие command/response. Дополнительных чтений,
записей, Uniwill flags/адресов и изменения checked finish/rollback нет.
Контракт: `docs/ACTION-LOGGING.md`.
# Штатные CPU-профили,26.10.2026

LaptopControl.setStockCpuProfile(0/1) использует родные Balanced/Performance,
HWMI0F04 с проверкой0E04 и прежним guard AC/board. Повторный явный выбор
посылает только запрошенный режим один раз, не противоположный импульс.
Audit wrapper журналирует команду и результат; readback флага не доказывает
физическое исполнение лимитов SMU. Minimal/Custom остаются собственными SMU
командами, FAN APIs не вызываются. Linux writer этим не добавляется.
# Общий KEYs lifecycle,27.10.2026

Fn Lock после явного checked выбора сохраняется в settings/keys-settings.ini.
Startup/resume использует общий core KeysLifecycle и прежний typed HWMI/sysfs
setter с fresh capability/readback; произвольные EC-регистры не используются.
Предпросмотр другой модели не разрешает аппаратные Huawei-команды.
