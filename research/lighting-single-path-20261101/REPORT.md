# Подсветка: общий путь GUI, CLI и восстановления

База `52d2975a`, продукт 0.1.424. Пакет от01.11.2026. Задача владельца:
свести подсветку всех подключённых моделей и обеих ОС к общим входам,
удалить старые пути, сохранить геометрию/скролл; автономные тесты, общий build,
обе portable и commit. Реальное оборудование и приложение не запускаются.

## Реальные расхождения

1. GUI использовал lighting policy, CLI SingleZone собирал свой raw change.
   Включение после Off в CLI обходило восстановление сохранённого эффекта.
   Цветовые преобразования и factory-default выбор тоже повторялись в CLI.
2. GM CLI применял клавиатуру без общего сохранения свежего состояния; GUI
   сохранял его в model-specific preferences. Последующий restore мог вернуть
   прежний выбор. Теперь explicit CLI использует rememberLightingChoice, как GUI.
3. Для native brightness/timeout Linux содержал отдельные .h/.cpp и wrapper
   в SettingsServicesLinux поверх двух native setters. Парная команда пыталась
   оба поля, но успех второго мог заменить сообщение об ошибке первого.
4. Было девять публичных Windows applyLighting* входов и два file forwarding
   helper. Внешние restore/cycle обращались к raw helper напрямую.

## Что сделано

Один переносимый LightingWriteRequest: Level/Timeout, Effect, Palette, CustomColor,
Native, Cycle, Zone, Update, Pulse и native pair. Пустой запрос — отказ, а не Off.
Origin различает user/restore/temporary/cycle; он не является аппаратным допуском.

Windows GUI, CLI, внешний Wujie restore/индикация и cycle callbacks входят в
executeLightingResource(LightingControlContext,request). SingleZone/HID policy
приватна в существующей LightingControlWindows.cpp. Raw Update сохраняет exact
expected snapshot/powerDisabled/preserveEffectsWhenBlanked и прежние guards.
Нативный запрос делегируется portable executeLightingResource(LaptopControl,request),
который используют GUI/CLI обеих ОС. Readback predicate native полей тоже общий.

Удалены LightingControlLinux.h/.cpp, публичный Linux wrapper, старый
LaptopLightingCommand/writeLaptopLightingResource и девять публичных Windows
applyLighting* declarations. Два cycle-file forwarding helper удалены; используются
существующие core load/saveKeyboardCycleSettings. Не добавлены production файлы,
вторые очереди, workers, transports или compatibility aliases.

Native pair сохраняет порядок и независимую попытку двух полей, накапливает ошибки.
Windows adapter превращает неуспех native result в прежний exception/completion
путь. Model setters сохраняют собственную проверку диапазонов и native readback;
generic helper не добавляет лишний полный hardware poll. Linux final snapshot
сравнивается прежним worker через общий predicate.

## Карта исполнения

| Источник | Общий вход | Реальный владелец/завершение |
|---|---|---|
| Windows SETs | SettingsAction -> executeLightingResource(context,request) | private policy -> native/SingleZone/HID; прежний SettingsExecution snapshot/classification/persistence |
| Windows CLI keyboard | literal arguments -> тот же request/entry | fresh SingleZone observation -> rememberLightingChoice для Wujie и GM; native/HID сохраняют собственную policy |
| Windows CLI lighting set | Zone request | прежний IteLightingController::apply, fresh zone admission, per-zone file |
| Wujie startup/resume/temporary indication | Update request с origin | прежние exact snapshots, owned restore, core journal и epoch |
| Host cycle | Cycle/Effect request; ticks как Update | прежний KeyboardColorCycle actor/scheduler/ownership; tick не становится новым сохранённым RGB |
| Linux SETs/CLI | portable native entry | прежний LaptopControl/sysfs setter, independent pair errors; worker readback/epoch без новых writers |
| GM внутренний keyboard restore | тот же device owner | прежний checked applyKeyboardBacklight; не дополнительный frontend policy |

## Модели и неизменённые границы

- Wujie, GM6, GM5 и L16: прежние SingleZone identity, byte masks, палитры,
  factory цвета, pulse codec, Off/native idle, owned rollback. GUI-порядок
  цветов не менялся. Конкретные firmware preset значения остаются model-specific.
- HYDROC keyboard/lightbar/logo/hinge: только уже обнаруженные контроллером зоны,
  прежние80H/firmware/descriptor checks и08/14 command order. Zone Off не становится
  глобальным Off; Save=0, RGB observation не выдумывается из сохранённого config.
- Honor Windows/Linux: только native Level/Timeout с собственными0/1/2 bounds,
  selected/shadow/idle semantics и backend readback. Нет заимствования RGB/EC maps.
- Остальные профили и Unknown: отсутствие backend/зоны остаётся отказом. Каталог,
  preview и HID descriptor сами по себе не выдают разрешение на запись.
- Linux SingleZone/HID writers не добавлены из Windows по сходству модели.

## Проверки

Расширен существующий tests/architecture_audit.cmake, без второго checker.
Он проверяет запрещённые frontend/raw writers, private lighting policy boundary,
отсутствие удалённых файлов/API, общий CLI entry/completion и прежние CPU guards.
Это source regression guard, не полный C++ call graph и не аппаратный тест.

В существующий offline-проект добавлены две фикстуры:

- native lighting, обе ОС: порядок setters, отдельные/парные ошибки, диапазоны,
  empty/unsupported requests, отсутствие/readback predicate; exact restore DTO;
  четыре zone request, zone-specific Off/Save0 и descriptor-only permission.
- Windows SingleZone: настоящий resource/policy/core journal через IEcBackend
  с массивом в памяти и deterministic SMBIOS fixture. Все четыре семьи проверяют
  GUI adapter/CLI request parity, Off/wake pulse, preset maps, соседние FAN/USB/
  lightbar bits, RGB codec/completion, temporary/foreign restore, rollback после
  ошибки второго RGB поля, недоступную HID-зону, software cycle и sleep guard.
  Все17 записей каталога проверены на отказ при отсутствии транспорта.

Windows:5/5, Linux:3/3 включая прежние runtime/CPU regression checks. Старая
Windows settings_queue fixture и Linux HLYL policy fixture адаптированы и
скомпилированы, их более широкие наборы не запускались. Native tests не создают
настоящие controllers; Memory EC не обращается к системным драйверам.

SOURCE-OBSERVATIONS.json:161 сравнение, включая все GUI/protocol source hashes,
core scheduler/journal, реальные native/HID/GM controllers и22 неизменённых policy
body. Wujie restore отличается только вызовами общего entry/request constructors.

Физические цвета, яркость, эффекты, сон и HID transport на настоящих ноутбуках
этими проверками не подтверждаются. Никакие прошивки/EC/SYS/OEM tools не запускались.
CI, лицензия, Git artifact policy и GitHub publication остаются вне пакета.

Общий build, фактические portable и четыре SHA-256 фиксируются в VERIFICATION.json.

Итоговый общий Release0.1.425 завершён: Windows/Linux GUI+mctl обновлены в обеих
рабочих portable. Четыре бинарника независимо сверяются с build outputs/manifest;
sidecars и Windows VERSIONINFO согласованы. Новые production файлы не добавлены,
две прежние Linux реализации удалены. Архивы и публикация не выполнялись.
