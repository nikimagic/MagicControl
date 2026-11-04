# Ревизия ядра и маршрутов управления

30.10.2026. Статический разбор. База: f17042fd, MagicControl 0.1.401,
обе рабочие portable показывают 0.1.401. Дополнительно просмотрен текущий
незакоммиченный перенос FANs/SETs, включая ResourceWorker/ResourceDispatch.
Его нельзя считать проверенной частью собранной 0.1.401.
Ссылки на строки основных находок относятся к базе0.1.401; текущие добавления
include/worker могут сдвигать строки. Во время ревизии рабочее дерево продолжало
меняться вне этой задачи; статус переноса executor уточнён ниже отдельно.

Исходники, аппаратные команды и настройки в рамках этой ревизии не менялись.
Тесты, сборка и обращения к аппаратуре не запускались. Ниже — подтверждённые
кодом маршруты и последствия, а не утверждение о воспроизведении на железе.

## Вердикт

Переход существенно продвинулся, но критерий «один владелец, один путь каждой
операции» ещё не выполнен. Общие реализации аппаратных команд существуют;
проблема главным образом в разных входах, завершении и сохранении, а не в
нескольких копиях одного и того же EC writer. Переименовать файлы или добавить
ещё одну обёртку недостаточно.

## Основные находки

### R1 — P1: CLI выбирает другой Custom-пресет

`src/cli/CurrentCommands.cpp:311` передаёт `number(...,1,3)` прямо в
PowerWrite::Preset. В CustomModeWindows.cpp writePowerPreset передаёт slot
без преобразования в Gm6Controller::useCustomPreset.

Контроллер использует индексы 0–2: requireCustomPreset в
`src/devices/mechrevo/gm6ir0c/ControlWindows.cpp:184`, useCustomPreset:1432,
доступ к slots[slot]:1435. GUI applyCustomPreset использует именно 0–2.

Следствие: `mctl power slot 1 --yes` выбирает второй пресет, slot2 — третий,
slot3 отвергается. Это также другая кривая вентиляторов, не только мощность.
Область: RAM fan/power profiles (GM6/GM5/L16/HYDROC) через соответствующую CLI
ветку. Команды в ходе проверки НЕ выполнялись.

Нужен один перевод внешнего номера в внутренний слот на границе запроса,
затем общий путь применения; аппаратные setCurves/applyNativeProfile не менять.

### R2 — P2: применение из CLI не имеет общего с GUI сохранения

На Wujie GUI после применения запускает rememberSettingsChoice:
`src/platform/windows/SettingsPersistenceWindows.cpp:54` — затем
rememberObservedBattery:19, rememberObservedPower:34 либо
rememberLightingChoice (`LightingControlWindows.cpp:181`).

CLI battery limit (`CurrentCommands.cpp:350`) вызывает executeBatteryResource,
но не сохраняет ChargeLimit через этот completion. Его Wujie backend
applyBatteryChargeLimit после readback возвращает результат, не записывая
сам процент в DevicePreferences (`BatteryChargeWindows.cpp:260`). Сохранение
ROM/platform ownership внутри backend не заменяет сохранение выбранного лимита.

Аналогично Wujie power использует applyStandardPowerProfile/executePowerResource,
keyboard level/effect/color — applyLighting (`CurrentCommands.cpp:399`), без
соответствующего rememberSettingsChoice/rememberLightingChoice. Проверенные
низкоуровневые Performance/KeyboardBacklight implementations сами эти поля
не сохраняют. Для других моделей часть persistence встроена в контроллер;
нельзя переносить этот вывод автоматически на GM/Honor.

Следствие: аппаратное действие может быть подтверждено, но старый выбор GUI
остаётся в settings и затем воспроизводится ConfigurationRestore при следующем
запуске/пробуждении. «No startup restore» в CLI help описывает запуск CLI,
но не объясняет различие persistence между одинаковыми явными командами.

Нужен единый command completion/readback/persist с определённой семантикой
CLI, GUI и restore. Не добавлять второй INI writer в CurrentCommands.

### R3 — P2, архитектурный контракт: restore мощности/RGB обходит ресурсный вход

У Wujie восстановление батареи уже исправлено: executeBatteryResource
в `devices/mechrevo/x5sp4nag/windows/ConfigurationRestoreWindows.cpp:270`.
Старый известный battery bypass в этом месте отсутствует.

Но в том же файле остаются прямые вызовы applyApuAutomaticLimits:239,
applyApuStockProfile:244, applyApuAllowedLimits:250. Подсветка восстанавливается
через локальный applyKeyboard:68 и applyKeyboardBacklight:81,284,289,296,
минуя applyLighting/SettingsAction resource entry, используемый GUI/CLI.

Это НЕ вторая реализация EC протокола: общий low-level writer и его guard
сохраняются. Это другой orchestration path, где admission, преобразование
saved intent и completion приходится удерживать согласованными отдельно.
Временная индикация подключения адаптера также имеет специальный lifecycle
контракт; её нельзя бездумно превратить в обычный сохраняемый пользовательский
выбор или убрать KeyboardLightingPause/expected-state checks.

### R4 — P2: GPU имеет обход общего входа и общей блокировки операций

GUI использует runDiscreteGpuOperation -> applyGpuPowerResource -> DiscreteGpu
(`src/platform/common/DiscreteGpuOperation.cpp:15`). CLI в
`CurrentCommands.cpp:551` вызывает DiscreteGpu::set/resetPowerLimit напрямую.
Общий аппаратный provider один, но жизненный цикл команды различается.

Кроме того, GUI start в Windows NvidiaGpuPage.cpp:78 и Linux
NvidiaGpuPage.cpp:55 создаёт собственный std::thread; runDiscreteGpuOperation
не получает DeviceOperation reservation. Его наличие не проверяют и
DiscreteGpu::set/resetPowerLimit. Нижние provider mutex защищают экземпляр
provider, а не общую операцию приложения.

MainWindow обрабатывает смену DeviceOperation доступности только для
SETs/FANs/KEYs (`MainWindow.cpp:644`). GPU страницы не входят в эту проверку.
Следствие: GUI-команду GPU можно отправить при уже занятом общем operation gate,
например во время другого аппаратного задания; общий gate её не исключает.
Фактическая коллизия с vendor reader на железе не проверялась.

Нужна одна GPU command operation для GUI/CLI с admission/result, при сохранении
действующих NVML/ADLX/sysfs backend, их диапазонов и readback.

### R5 — P2: Linux не обеспечивает одного владельца между GUI и CLI

Windows CLI захватывает applicationInstanceMutex для writes
(`CurrentCommands.cpp:116`). В Linux соответствующая ветка — `(void)writes`
на строке121. GtkApplication обеспечивает GUI single-instance, но CLI не
участвует в его D-Bus ownership и не пересылает команды в GUI.

DeviceOperationGate — статический объект процесса. HLYL gate прямо объявлен
process-local (`src/platform/linux/huawei-wmi/HlylOperationGate.h:4`). Поэтому
Linux GUI и CLI могут одновременно управлять одним typed backend и сохранять
его настройки. Последовательность из нескольких sysfs операций и readback
не защищена общей межпроцессной арендой. Это не утверждение о конкретном сбое
Linux kernel driver; недостаёт именно application ownership.

### R6 — P2: legacy capture Wujie пропускает новый верхний предел 60%

`ConfigurationRestoreWindows.cpp:39` допускает только 0/100/95/85/10.
Поддерживаемый теперь 60 отсутствует. При отсутствии сохранённого ChargeLimit
и фактическом EC=60 migration capture не добавляет значение в DevicePreferences;
цикл заполнения missing fields его пропускает, savedBatteryWriteRequest не
получает порог для последующего restore.

Область — первоначальное заполнение/миграция неполного файла, а не обычное
восстановление уже сохранённого ChargeLimit=60. Старый 10 может оставаться
совместимым значением; исправление не должно переписывать пользовательские файлы.
Перечень распознаваемых порогов нужно брать из общей политики, а не дублировать.

## Незавершённая архитектура и гигиена

1. В собранной 0.1.401 ResourceControlTable — таблица типизированных обработчиков,
   не универсальный command executor. В этой базе KEYs (KeysLifecycle.cpp:240),
   INFO (InfoDumpResource.cpp:115) и GPU имеют свои исполнители. В ходе ревизии
   незакоммиченный перенос расширился: FANs/SETs и затем KEYs используют общий
   ResourceWorker, INFO переводится на submitDetachedResource, а длительные
   циклы — на ResourceActor. Пул получил отдельную interactive lane. Это реальная
   работа по централизации runtime, но она пока не является проверенной частью
   0.1.401 и сама по себе не исправляет R1–R6. Нельзя выдавать централизацию
   создания потоков за единую семантику apply/readback/persist каждой операции.
   Само количество потоков НЕ дефект: длительные охлаждающие циклы, watchdog и
   OS event monitor должны сохранять свои контракты и не занимать навсегда
   ограниченный пул коротких команд.
2. Перенос из GUI частично остаётся физическим переносом файла. Platform
   SettingsOperationCoordinator включает GUI SettingsCommandModel, SettingsNotice,
   Text и FooterStatus; Windows FAN service включает MonitoringCards/OsdMessages
   и имеет UI callbacks positionControls/updateEditorText. Linux SettingsServices
   включает gui/common/BatteryResourceState. Это обратная зависимость служебного
   слоя от GUI, а не полностью независимый domain/application layer.
3. ResourceState честно является принятым состоянием конкретного потребителя,
   а не единой process-wide базой всей машины. Несколько неизменяемых views,
   ingestion DTO и presentation caches сами по себе НЕ дубли hardware owners.
   Но общий machine-state service для всех вкладок этим ещё не создан.
4. Есть два неиспользуемых production headers: gui/windows/DiscreteSlider.h
   и gui/linux/DiscreteSlider.h. Их include/function names встречаются только
   внутри самих файлов; в src/tests потребителей не найдено. Это реальные
   кандидаты на удаление, но они не исполняются в рабочем продукте.
5. Все 193 production .cpp (без runtime/third_party) перечислены в CMake.
   Массы оставленных вне сборки .cpp не найдено. Архивные пути не подключены
   к production targets. Заголовки и функции не объявлялись мёртвыми только
   потому, что отсутствуют в CMake.
6. Крупные монолиты остаются: GM controller 1582 строки, Wujie FanController
   1696 строк. Их размер не доказывает дублирования протокола. Разделять можно
   владение состоянием/политику/кодек, не создавать второй контроллер вентиляторов.

## Что в новой архитектуре действительно работает

- ResourceState/ResourcePresentationView дают неизменяемое поколение наблюдений;
  каталог не выдаёт разрешение на запись и не подменяет неизвестное нулём.
- DeviceClass появился; capabilities представлены декларациями данных вместо
  прежних шести полей bool. Exact SMBIOS и runtime identity guards сохранены.
- Ключевые аппаратные реализации не размножены: общий byte transport EcAccess,
  один applyKeyboardBacklight, общий Wujie Performance codec; разные board
  adapters — не дубли одной реализации. Фабрики GM/Honor/HPBSD используют
  shared/weak controllers внутри процесса, не отдельный controller каждой вкладки.
- KEYs имеет общий KeysOperations, отдельную KeysPreferences и общий
  completeKeyResource для явных команд. Lifecycle policy остаётся самостоятельным
  источником событий, использующим тот же hardware adapter; его нельзя механически
  признать мусором и удалить.
- INFO имеет общий InfoOperation и общий inventory/dump collector для GUI/CLI.
  Detached worker держит shared lifetime задачи и аппаратных capabilities;
  это осознанный контракт отмены медленного WMI, не забытый GUI thread.

## Порядок исправления без изменения команд железа

Первый блок: R1/R2/R6 — индексы и общий completion/persistence/пороговая политика.
Второй блок: R3/R4/R5 — единые application entries для restore/GPU/CLI, Linux
ownership. Третий блок: завершить FANs/SETs separation и привести executor,
удалить два dead headers и обновить устаревшие описания. Вентиляторные таблицы,
тайминги, protect/ownership checks, EC адреса и драйверы при этом не менять.

Эта ревизия НЕ является разрешением считать любой из этих переносов завершённым
или проверенным на всех устройствах. Нужны отдельные проверки после исправления;
в текущем задании они не запускались.
