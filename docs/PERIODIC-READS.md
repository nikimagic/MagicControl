# Периодические чтения MagicControl

Снимок кода30.10.2026,0.1.394. Основная проверенная платформа здесь — Windows
Wujie X5SP4NAG. Частота ниже означает расписание вызовов приложения, а не
гарантированную частоту измерений внутри чипа или драйвера. Занятость очереди,
сон, недоступность транспорта и таймауты могут пропускать очередной цикл.

## Общий мониторинг FANs

Один sensor timer1000ms вызывает `pollSensors` и общий `readFanMonitoring`.
Если вкладка скрыта, без логирования timer/чтение останавливается. В фоне с
логированием — обычно5сек, с battery logging или активным diagnostic burst —
1сек. Занятый reader не запускается вторым экземпляром поверх себя.

| Что читается | Источник на Wujie | Период/кэш |
|---|---|---|
|CPU/GPU RPM и PWM, температура EC|UWACPIDriver / EC fan telemetry|общий цикл FANs1сек|
|CPU загрузка, текущая частота|Windows / AMD ADLX, выбор провайдера|общий цикл1сек|
|CPU Tctl/Tdie, SoC|PawnIO, AMD telemetry|общий цикл1сек|
|CPU core/package энергия и рассчитанные ватты|PawnIO MSR energy deltas|общий цикл1сек; это отдельное чтение от SMU limits SETs|
|GPU загрузка, частота, температура|AMD ADLX / ADL PMLog|общий цикл1сек; значения могут быть кэшем драйвера|
|GPU мощность и доступные дополнительные датчики|выбранный AMD provider/domain, не безусловно весь ASIC|общий цикл1сек|
|NPU загрузка/частота|AMD ADLX, если доступны|общий цикл1сек|
|NPU мощность|AMD XRT|5сек при наличии значения;30сек при отсутствии/ошибке;N/A не превращается в0Вт|
|Windows батарея: SOC, статус, напряжение, мощность, остаточная/полная/проектная ёмкость, здоровье, циклы, оценка времени, температура|Battery Class IOCTL|при чтении батареи; недоступные поля остаются неизвестными|
|Поиск/перечень Windows battery devices и tag|Battery Class enumeration|до10сек; при ошибке повтор раньше|
|EC батарея: SOC, исходный ток/расчёт, напряжение, температура, ёмкости, циклы, пороги и флаги|точный EC battery decoder|из FANs/SETs; общий coherent-cache800мс уменьшает повторные чтения|
|Температуры модулей RAM|PawnIO Intel I801/AMD PIIX4, SPD5118; Linux hwmon|без фильтра по модели; Intel обновление 5с, AMD в общем цикле; поиск хабов 0x50–0x57 раз в 60с или через 10с при неудаче; HPBSD сохраняет свою политику страниц|
|SMBIOS идентификаторы/серийники RAM|Windows SMBIOS|кэш1минута|
|Температуры/идентификаторы дисков|Windows Storage property, NVMe SMART и доступные fallback|кэш5сек; diagnostic mode может обходить кэш|
|Экспериментальное потребление системы|EC44C и существующая модель расчёта|цикл мониторинга; последнее значение ограничено10сек; единицы/домен не равны автоматически CPU/GPU|

На других устройствах тот же monitoring entry подключает только подходящие
providers: Intel PawnIO/RAPL/PCH, Intel IGCL, NVIDIA NVAPI/NVML, Honor HWMI,
HPBSD ITE5571. Intel I801 RAM reader имеет внутренний5-секундный кэш.
Наличие группы в таблице не означает наличие датчика на каждом устройстве.
После сбоев providers используют свои retry delays, часто30сек, вместо
беспрерывного повторного открытия драйвера/API.

Код: `src/gui/windows/FansPage.cpp` kPollIntervalMs/pollSensors,
`src/platform/windows/FanMonitoringWindows.cpp`, `MonitoringWindows.cpp`,
`AmdTelemetryWindows.cpp`, `pawnio/PawnTelemetryWindows.cpp`,
`pawnio/IntelTelemetryWindows.cpp`, `pawnio/IntelMemoryWindows.cpp`,
`NvidiaSensorsWindows.cpp`, `IntelGpuWindows.cpp`.

## Карточки SETs и трей

| Что читается | Расписание |
|---|---|
|Выбранный CPU profile751, overrides783/784/785, заводские Office/Gaming/Turbo таблицы|основной SETs poll3сек при видимой вкладке|
|Итоговые STAPM/Slow/Fast из EC46A/46B/46F|тот же полный SETs poll|
|SMU/PawnIO Fast/Slow/STAPM limits и Fast power для сверки|тот же полный SETs poll; не каждое секундное обновление нового счётчика|
|Зарядные политики/порог, Platform/Project/ROM ID, адаптер/Type-C флаги и рейтинг, показания батареи|тот же полный SETs poll, с model-specific controllers|
|Подсветка:78C/767/7C5 и RGB769..76B, статус доступности OEM writer|тот же полный SETs poll|
|Счётчик выключения подсветки Wujie|1сек: короткое чтение через ту же очередь SETs; полный poll заменяет короткий на своём тике|
|Трей: батарея/профиль/яркость/доступность|5сек; дополнительно вызывает полный SETs refresh, включая скрытую вкладку|
|Внеочередной readback|после команд, открытия вкладки, событий питания/возобновления/аппаратных клавиш|

Поэтому «раз3сек» — период основного таймера, а не строгий минимальный интервал
для всех обращений к SMU/EC: существующий tray poll и события тоже запрашивают
данные. Несколько consumers могут обращаться к общим getter-ам, у которых есть
собственные кэши. Новая функция счётчика не устраняет и не учащает эти старые
триггеры; она не создаёт свой writer или отдельный polling thread.

Новый native getter читает741,6E6,7C3,8A5/8A6 (high/low/high для rollover),826,
490.0,825/827 и78C.1, после проверки exact-board/identity/backlight capability.
Это elapsed count, статус и текущая яркость, а не настройка таймаута. При
APP741.0 либо host-disable78C.1 поле показывает «Отключено», при истечении —0,
при недоступном/устаревшем снимке —«—». Изменение AC/DC не выводится из7C3.
Hardware mapping подключён только к подтверждённому Wujie. Linux SETs и другие
модели получают такой же read-only UI с«—», пока не подключён их проверенный
reader; чужие регистры не используются.

Код: `SettingsPage.cpp` readState/kRefreshTimer, `MainWindow.cpp`
kTrayRefreshTimer, `SettingsSnapshotWindows.cpp`,
`SettingsOperationCoordinator.cpp`, `ApuPowerWindows.cpp`,
`src/core/src/KeyboardBacklight.cpp` readKeyboardIdle.

## Контроллеры, KEYs и условные фоновые действия

| Потребитель | Чтение/условие |
|---|---|
|Wujie FAN controller|обычный wake loop1сек: флаги native/owned mode, подтверждение состояния и обратная связь; активный Manual online дополнительно использует температуры/PWM своего цикла|
|FAN recovery|после сна/потери готовности: идентификаторы, gate/owned состояние, при необходимости сверка RAM-таблиц; grace/retry intervals не являются постоянным сенсорным опросом|
|Слежение за адаптером Wujie|Windows GUID_ACDC_POWER_SOURCE/события питания; дополнительно GetSystemPowerStatus в обычных getter-ах. Wait1сек в indicateKeyboard — длительность временной индикации, не отдельный вечный AC poll|
|KEYs видимая вкладка|2сек: Fn/Windows/USB/AC/Copilot аппаратные переключатели по своему backend; Wi-Fi/Bluetooth/микрофон/камера/тачпад по Windows APIs; только OSD preference берётся из памяти|
|KEYs lifecycle|при startup/resume/apply; retry ожидание1сек только для незавершённого восстановления, не бесконечный общий аппаратный poll|
|Микрофон и mute LED|первичный getter ОС и дополнительный7A6.2 на допущенном Wujie. Windows audio event ставит notification в тот же KeysLifecycle worker; Observe отражает текущий mute в LED, не меняя звук. Нового периодического writer-а нет|
|Copilot|общая KEYs очередь/getter и KeysLifecycle startup/resume; отдельный5сек sync и F23/Win+C hook удалены|
|OSD/аппаратные уведомления|обработка событий; наблюдатель F24/lock keys не выполняет Copilot suppression|
|RGB Cycle6|только когда явно активен; worker просыпается каждые100мс для scheduling, читает/меняет effect по заданному интервалу (по умолчанию10сек);100мс не означает10 EC polls/сек|
|Журнал мощности|короткий burst после действий/событий: до10 samples с минимальным интервалом1сек; журналирование обычного monitoring snapshot не всегда означает второе чтение датчика|
|Отдельная GPU card|1сек, пока открыта; штатные vendor telemetry/limits getters|

Код: `FanControlWindows.cpp`, `ConfigurationRestoreWindows.cpp`,
`KeysPage.cpp`, `KeysHardwareWindows.cpp`, `src/core/src/KeysLifecycle.cpp`,
`CopilotFirmwareWindows.cpp`, `KeyboardBacklight.cpp`, `PowerDiagnosticsWindows.cpp`,
`src/gui/windows/NvidiaGpuPage.cpp` (общая GPU-page реализация по vendor).

## Что не входит в обычный периодический опрос

- Полная INFO-инвентаризация, SMBIOS/EDID/TPM/драйверы и сохранённый отчёт:
  отдельное обновление по кнопке, а не полный сбор каждую секунду.
- Полный EC/BIOS/VBIOS dump: по явному запуску сборщика. Их внутренние серии
  чтений действуют только во время соответствующего дампа.
- Произвольный адрес EC-логгера: по Enter, не постоянный полный скан EC.
- Проверка версии на GitHub/в папке обновлений: при запуске проверки в updater,
  а не сеть каждую секунду фонового monitoring loop.
-100мс scroll/paint/очередь и80мс progress timer обновлятора — UI/scheduling,
  не новые аппаратные измерения сами по себе.

Архитектурный факт: сейчас единого scheduler-а для всех read consumers нет.
Разные вкладки/контроллеры имеют собственные расписания, а транспортные
mutexes/DeviceOperation и внутренние caches согласуют доступ. Эта задача
добавляет только read-only наблюдение счётчика в уже существующую очередь;
глобальная перестройка расписаний и аппаратных команд не выполнялась.

Фон программы не состоит исключительно из read-only getters. Активные
FAN controllers, явный RGB Cycle6 и общий lifecycle restore
могут выполнять свои ранее существовавшие команды. Новый счётчик подсветки
их не вызывает и не получает права записи из-за наличия показания.
