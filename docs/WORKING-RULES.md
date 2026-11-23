# Действующие ограничения MagicControl

Этот документ раскрывает обязательные маршруты чтения из корневого AGENTS.md.
Нормативны ограничения поведения и допуска. Описания реализации проверены по
коду 31.10.2026; при изменении API сверять источник, а не восстанавливать старое
имя класса из отчёта. Новое поручение владельца имеет приоритет. Следы переноса
старых правил — research/navigation-package-20261031/RULE-COVERAGE.json.

<a id="r1-runtime"></a>
## R1. Runtime и ресурсные пути

- Один process-owned finite Runtime в ResourceControl.h: четыре worker, один
  reserved interactive. ResourceWorker — lane/result lifetime; ResourceDispatch —
  admission/очередь данных. GUI не создаёт thread/jthread/async/native worker,
  свои pending-result buffers, произвольные hardware lambdas и вторые setters.
- Постоянные FAN/restore/cycle/watchdog/event loops используют ResourceActor leases
  общего registry с прежними stop/notify/join, thread identity и алгоритмом.
  Они не занимают finite pool. Registry имеет process lifetime; orphan diagnostic
  RPC не должен создавать static-teardown join. Detached INFO владеет всеми
  захваченными capabilities; закрытие не ждёт unbounded WMI.
- TaskContext изолирует audit TLS. UI bindings выполняются на UI thread;
  PostMessage/GTK idle доставляет readiness, lifetime guard остаётся OS adapter.
  Владельцы SETs/FANs — SettingsOperationsWindows/Linux, FansOperationsWindows/Linux.
  Удалённые SettingsOperationCoordinator и Linux wrapper не возвращать alias-слоем.
- SETs/FANs owner fields приватны; GUI использует composition и const Presentation
  bindings на UI thread. Не обходить их через mutable references/const_cast или
  generic setters для queue/pending/observed state. Backend provider — construction
  dependency; preview seed не выдаёт real observation/capability. Draft edits и
  optimistic selection — typed owner commands, не GUI присваивания. Не возвращать
  удалённую SettingsBackendsWindows пару; test access только в isolated fixtures.
- При неуспехе сохранять порядок recovery/rollback/epoch/persistence и точный
  model admission. Отдельные presentation caches/viewport positions не являются
  новыми hardware owners и не должны разрешать запись по сохранённым данным.
- executeGpuOperation общий для GUI/CLI: typed request, preview/identity admission,
  DeviceOperation reservation, прежний provider/readback, audit/result. GpuOperations
  владеет provider/lane/cache обеих GUI. Не вызывать setter напрямую из CLI.
  Сохранять NVIDIA/Radeon единицы/диапазоны; писать только исходным provider ID,
  не physicalId, именем, vendor или выбранным монитором гибридной конфигурации.
- Подсветка: общий LightingWriteRequest и executeLightingResource для GUI/CLI/
  внешнего restore/cycle. Native Level/Timeout используют один portable setter path;
  SingleZone/HID policy приватна у Windows owner. Не возвращать публичные applyLighting*
  wrappers, LightingControlLinux пару или raw RGB policy в CLI/GUI. Raw Update —
  только existing lifecycle/cycle с expected/epoch guards, не обход capability.
  Цветовые таблицы, masks, HID Save=0, per-zone files и native idle сохранять.
  После explicit fresh observation — rememberLightingChoice; restore/temporary/tick
  не становятся новым выбором. У отсутствующего backend нет чужого fallback.
- update::Operations один для Windows/GTK: release/progress/stop/completion,
  ResourceWorker; перевод только UI через UpdatePresentation. Проверки пакета,
  normal exit/helper, отмену и сроки операций не обходить новой реализацией.
- Windows OSD WindowEventLoop использует core actor для ready/GetMessage/close/join
  и cleanup на том же thread; renderer не переносить в pool. Linux сохраняет
  native GTK/GDBus dispatch. OS waiters/reapers AMI/NVFlash/PawnIO не превращать
  в GUI executor и не менять их lifetime ради числа упоминаний thread.
- Sc: один DisplayRotationOperations с parity очереди/token/timestamp/result.
  Windows CCD и Linux KDE/KScreen либо X11/XRandR сохраняют native readback.
  Только однозначная активная внутренняя панель, +180°, без внешнего/primary
  fallback и без XWayland подмены Wayland. Linux global binding — portal с
  согласием пользователя, GTK release только при отсутствии binding. KEY_UNKNOWN
  не считать Sc; не читать/логировать клавиатуру и не патчить driver/EC. Неизвестный
  compositor, отсутствие разрешения или kernel keymap — явная недоступность.
- Служебные core/platform/device/protocol/update слои не включают GUI headers.
  Native IDs/OSD title/renderer language — view; worker получает typed semantics.
  SettingsCommandModel с HWND ID decoding в сервис не возвращать. Общие core
  data contracts (BatteryCardView/FAN projection/OSD payload) не копировать в GUI;
  UTF8/UTF16 OS adapter — TextWindows.

<a id="r2-info"></a>
## R2. INFO и firmware tools

- INFO доступна всем моделям, Unknown, inventory-only и preview; описывает реальную
  машину. Capability/reader admission остаётся внутри сборщика, отказ одного
  reader не разрешает чужой транспорт/прошивку.
- Один InfoOperation/executeInfoDumpResource lifecycle: запрос -> core job ->
  collector -> audit/transcript/result. GUI не заводит Job/InventoryJob/DumpJob,
  stop_source, worker или второе завершение журнала. Native poll — только доставка.
- executeInfoInventoryResource выполняет collect/cancel/save/result для GUI/CLI;
  страница повторно не сохраняет cache. Collect-only API допустим для явного чтения,
  не для прямого InventoryCache::save из frontend. Terminal result публикуется
  один раз под resultGate; поздние callbacks/исключения его не переписывают.
  Raw PawnIO callback остаётся в audit.
- InventoryCache GUI/CLI использует один межпроцессный read/publication lease.
  FileStorage в существующем storage leaf: уникальный exclusive temporary,
  flush/close, сериализованная atomic publication; не возвращать общий .tmp.
  Активные три пары логов защищены отдельными OS leases, выбор/legacy migration
  сериализованы. Не удалять стабильные .locks и не trunc занятую пару.
- CLI --read-only отклоняет управляющие команды до Runtime/backend init, включая
  сочетание с --yes. Файловые диагностические результаты остаются разрешены.
- Windows installer: verified prepare на worker, UI-apartment launch, проверка
  установленной версии. Отмена/закрытие не запускает уже подготовленный installer.
  Windows reader не становится Linux reader. Удалённые InfoWorkerWindows и
  BiosOperationsLinux не возвращать wrappers.
- Сохранять разницу ordinary VBIOS/EEPROM, successful/partial/failed/cancelled,
  полноты образа и физического flash. Readback/hash не доказывает boot/recovery.
  Официальные AFU/FPT16/FPT19/SYS references встроены offline; не скачивать их
  во время BIOS-dump. Не запускать OEM helpers ради проверки своих предположений.

<a id="r3-keys"></a>
## R3. KEYs, Copilot, микрофон

- Системные KEYs доступны на реальной машине независимо от модели, preview,
  EC/FAN/APU отказа. Недоступный аппаратный адаптер объяснять в его строке,
  не выключать всю вкладку и не заимствовать неподтверждённый transport.
- Один KeysOperations владеет explicit queue/lane, DeviceOperation, radio prepare,
  read-overlay и completion data. KeysLifecycle сохраняет отдельную startup/resume/
  observe policy (retry/suspend), использует те же setters. Не создавать replay
  loops в FAN/power controllers. Подготовка OSD/preferences/radio — UI-affine;
  hardware callbacks — worker; typed context живёт до join.
- Явная команда подтверждается fresh observation и completeKeyResource перед
  одним writer keys-settings.ini. Не подтверждать ожидаемым значением или старым
  presentation cache. Более новые внешние изменения других строк переживают
  baseline операции; собственная строка требует свежего readback. Локальный OSD
  preference применяется после overlays.
- KEYs имеет KeysPreferences из optional<bool> и свой 0/1 reader/writer. Не
  переиспользовать DevicePreferences/его validators и power/battery schema.
  Общим остаётся атомарный файловый writer. OSD/Copilot — keys-settings.ini;
  app-settings.ini только общие font/log/language. Legacy перенос — с backup,
  DTO не является вторым хранилищем. Fn/mic сохранять по общему mapping после
  fresh readback. Bluetooth не replay-ить при startup/update/resume; менять
  только явной GUI/CLI командой. См. KEYS-PERSISTENCE.md.
- Copilot — обычный native resource copilot-lock/applyKeyHardware, одинаковый
  для GUI/CLI/lifecycle. Один CopilotBlocked writer. Никаких CopilotKeyBlocker,
  F23/Win+C hooks, software fallback, GUI watchdog/пятисекундного опроса или
  отдельного startup/resume replay. Аппаратный getter — источник enabled.
- Exact Wujie82/72/26: On использует728.2 и общий741.0, сохраняя соседние биты.
  Off снимает728.2 даже при отсутствии741.0, но не снимает общий741.0. Для других
  устройств — их typed transport; OEM OemWMIfun ABI64-byte03/05 getter,
  03/06+bool setter, status0/state0|1 не переносить на Huawei/HPBSD как raw EC map.
  Нет native поддержки — нет записи. Не менять F24/touchpad/сочетания, FAN/RAM/PL,
  зарядку; не запускать OEM helper EXE, не делать keylogging.
- Микрофон: один resource handler, enabled из подтверждённого Windows capture
  endpoint. Exact Wujie SetLED зеркалит mute в7A6.2 (mute1/unmute0), только mask04;
  battery7A6[5:4], touchpad.3/.6 и прочие биты сохранять. Это LED, не аппаратное
  отключение микрофона; не переносить карту на другие модели. Ошибка LED/EC не
  блокирует системный mic и никогда не unmute/rollback подтверждённого mute.
  Внешний audio event проходит через KeysLifecycle.observe и следует свежему
  текущему состоянию; не replay старого bool и не менять Windows mute из LED
  callback. Без новых timers/hooks/GUI EC writers/LED preference file.

<a id="r4-fans"></a>
## R4. FANs и Wujie Default

01.11.2026: активное число точек — DeviceProfile.fanPointCounts, default16/16
у всех моделей. HPBSD8/3; Honor10/10 описывает штатные read-only ступени,
пользовательская запись такой таблицы ещё не допущена. Существующий
FanCurveProfiles хранит все16 ячеек и предоставляет setActivePointCount;
не создавать отдельный8-point пользовательский класс/пресет/restore.
Неактивные ячейки сохранять. Значения кривых не ресэмплировать и не заменять.
Custom использует один curves payload; прежний native-блок v3 читается только
для точного импорта. ConfigurationRestore общий для EC и typed backends;
startup/resume воспроизводит подтверждённый выбор через существующие resource
entries, не делает новый выбор и не угадывает отсутствующий legacy mode.

FAN command contract — FanWriteRequest/executeFanResource. GUI/CLI/внешний restore/
согласованный Custom используют один entry. Не возвращать writeGm/Native/EcFanResource,
per-backend workers или applyEc* wrappers. Typed queue хранит тот же request.
Pending не равен confirmed; CLI wait/readback внутри owner. Existing operation
передаётся в очередь, не захватывается повторно против собственного lease.
Manual EC и Manual online разные; Online отправляет profiles одной операцией,
не двумя перезаписываемыми channel requests. Restore/Compound не сбрасывают
APP/Copilot как явный Default. Internal device replay остаётся у exact owner.

Перед FAN/sleep/start/exit/profile изменениями прочитать
[Функциональный контракт](../src/platform/windows/FAN_MODES.md).

- Input limits всех моделей/ОС: FanControlLimits,20–100°C/5–100% inclusive для
  создания, режима, стрелок/колеса, ввода и подтверждения. Не сужать их локальными
  model/codec limits, не возвращать30–80°C, minima7/25/36/38 или0/255 в input.
  Comment/static_assert владельца сохранять. Перед общим билдом статически
  сверять края20/100 и5/100 и отсутствие local overrides в обеих GUI.
  В Manual пользовательские поля
  доступны; обязательные защитные ступени показывать отдельно от серых inputs.
- Границы inputs не разрешают пересчёт/замену/сэмплирование старых кривых,
  изменение числа точек, algorithms/native-host modes/presets/hardware paths.
  Сохранённые кривые сохранять побайтно. Read-only telemetry/factory tables
  показывают фактические значения; служебные0/255 остаются codec markers.
- Default-view только read-only через NativeDefaultFan и accepted ResourceState.
  Factory-кривая не Manual/receipt, не записывается в fan-curves.csv/Custom.
  Для выбранного Wujie62C0 оба графика используют CPU EC43E/9178; ADLX GPU/Tctl
  не заменяют этот input. Второй запрос — расчёт transfer function, помечать≈,
  не выдавать за отдельный прочитанный регистр.6598/730 — APU power, не FAN.
- Подтверждённые Manual/Boost/иная identity/table убирают Default-view. Ошибка
  чтения не подтверждает смену режима: удерживать последний display sample до10с
  с исходным временем и retained marker, не обнулять обычный poll и не омолаживать
  старые значения. Без новых writers/mailbox/mode changes/GUI pollers; тот же renderer.
- Явный Wujie Default (GUI/CLI) вызывает core ApplicationPresence: FAN gates ->
  native Copilot Off -> fresh проверка потребителей -> APP741.0 Off последним ->
  CopilotBlocked0 в keys-settings.ini. Повторный явный Default тоже выполняет сброс.
  CPU/PL/RAM/Custom slots не менять. Не переносить последовательность на GM/Honor/HPBSD.
- Внутренний Default/recovery и обычный Copilot Off сохраняют узкий контракт,
  не освобождают APP чужих функций. Startup/resume не поднимает APP без потребителя;
  Manual restore остаётся у владельца. Старое безусловное удержание APP явным
  Default отменено; не возвращать его из исторического отчёта.

<a id="r5-power-battery"></a>
## R5. Мощность, батарея и идентификаторы

**Правило владельца01.11.2026: Custom един для всех устройств.** Один редактор,
одна модель пресетов1/2/3 (мощность вместе с собственными FAN-кривыми), единая
библиотека/selected/active и один executePowerResource(Custom/CustomEditor/Preset)
для GUI, CLI и внешнего restore. Не создавать model-specific Custom-системы,
отдельные UI/очереди/библиотеки пресетов, альтернативные setters или обходы.
Exact backend переводит общий запрос в свои команды, кодирует native-точки,
проверяет подтверждённые аппаратные ограничения, readback и rollback. Разное
число native-точек — данные общего пресета, не новая реализация Custom. Общий
диапазон запроса не доказывает физическую мощность; локальную границу тестилки
нельзя выдавать за предел BIOS/SMU. Не заимствовать чужую EC-карту/компенсацию.
Штатные Office/Gaming/Turbo или Balance/Performance, Default и Manual также
являются общими операциями: различия находятся в данных и device backend.

Обязательно прочитать [POWER_MODES](../src/core/src/POWER_MODES.md) для APU/SMU/
783–785/restore и [BATTERY](../src/platform/windows/BATTERY.md) для зарядки,
aging/USB-C/card/threshold/startup/resume. Реальные masks/ABI — у функции/codec.

- Порядок overrides: STAPM/Slow/Fast; EC request не равен гарантии SMU limits.
  Не добавлять непрошеный FAN replay к CPU-профилю. Существующий явный Custom
  preset может согласованно применить FAN/CPU через владельца: сохранить эту
  отдельную транзакцию, допуски и rollback, не подменять её общим запретом.
  Не возвращать прямые GUI/CLI EC writers или выходной сброс таблиц/режима.
- Внешние CLI slots1/2/3 переводятся в0/1/2 только на входе.
  CPU-команды SETs/CLI/внешнего restore идут через executePowerResource. Не
  возвращать StandardPowerModesWindows пару или отдельные public stock/editor/
  slot apply wrappers. Внутренний replay и физический whole-profile OEM cycle
  остаются в device owner с его прежними guards/codec/journal. Static boundary
  guard — tests/architecture_audit.cmake, не доказательство железа.
  Wujie explicit power/staticRGB используют свои common completion; батарея
  завершает fresh observation/persistence внутри executeBatteryResource. GUI/CLI/
  внешний restore не дублируют INI writer. Restore использует общие resource entries; временная
  индикация не сохраняется новым пользовательским выбором. Capture берёт текущую
  BatteryUiPolicy с60 и legacy10/raw0, не локальный список. Native модели сохраняют
  свою persistence policy. Нижний UI и backend setter/save/restore — разные вещи.
- Запись процента не доказывает удержание/возобновление зарядки. Общий battery API;
  зарядку не связывать с FAN tables/flags/modes и не назначать всем чужие platform/ROM.
  Родная identity требует evidence; первый снимок изменённого EC не фабричный факт.
  Имена Full Capacity/Balanced/Healthy и Project ID/Platform ID/ROM ID не переводить.
- BatteryWriteRequest один для Windows/native обеих ОС. Не возвращать отдельные
  applyBattery*/Honor/HLYL policy wrappers, raw charge-pair dispatcher или GUI
  SettingsRememberChoice::Battery. Native policy требует точного chargePolicyId,
  одной transaction для read/decision/write/save и собственного setter readback.
  Процент, нижний порог, штатное напряжение, экспертная ID policy и aging — разные
  операции. Preserve lower setter/save/restore, raw0/100 native meaning, Wujie97,
  native60 cap55; чужие ID/кодеки не унифицировать ради общего API.
- Aging/история/USB-C — через текущие core/typed APIs из BATTERY; не подменять BMS
  дату, health, счётчик времени или факт применения сохранённым намерением. Без
  confirmed transport/identity capability показывать недоступность, не fallback.
  Согласованные поля read-only history не обнулять и не выдавать вычисление за EC.
  USB-C priority сейчас unsupported: не обходить core admission, не сохранять
  отказанный режим и не возвращать его replay/скрытые controls попутно. Raw status
  не доказывает выбранную политику Performance, в том числе через I2EC.
- Startup identity читать до auto-apply. Истории rom_id/platform_id/project_id
  append-only с версией/временем/origin/readback; не ротировать и не использовать
  как автоматическое разрешение возврата identity. Обычные логи —3whole-session
  пары без.part; inventory cache в logs_<фактическая плата>/bios-inventory.json.
- Linux primary GUI и CLI writers держат один flock application lease; файл не
  удалять при release, иначе возможно два inode/владельца. Старые процессы и
  сторонний CC не участвуют. Системные CLI help/version не обращаются к железу;
  в SSH нет UAC/dialogs — права предоставляет вызывающий сеанс. Общие collectors,
  model/ownership guards и журналирование сохраняются; unknown остаётся отказом.

<a id="r6-ui"></a>
## R6. UI, scroll, геометрия

Перед изменением общих controls/layout/labels/меню прочитать
[LINUX-UI-PARITY](LINUX-UI-PARITY.md) и сверить обе реализации. Windows — визуальный
эталон; Linux hardware availability определяется реальным backend, не сходством UI.

- Переиспользовать common render/control/theme/OSD/buffer/clipping механику;
  новую возможность добавлять параметром/adapter существующего компонента.
  Новый профиль даёт данные/labels/capabilities, не новый renderer/дизайн/OSD window.
- ScrollModel+PageMechanics (Windows), общий scrolledWidget/GTK adapter (Linux).
  Отдельные positions/extent/viewport/native adjustment правильны. Не объединять
  позиции окон и не копировать wheel/page/drag/slider арифметику. Допустимы native
  EDIT/RichEdit/HWND bindings, viewport coordinates, строки↔пиксели.
- Не двигать/сужать карточки ради полосы, не добавлять gutter/padding/повторное
  вычитание ширины. Full-width geometry, scrollbar справа поверх viewport,
  Y-offset ровно один раз для карточек и controls. Это относится всем SETs cards,
  включая внешний Logger; вложенные cards сохраняют собственные отступы.
- Сохранять округления, ширины, колесо, hit areas, размеры/положение/отрисовку,
  шапку, палитру и DPI. Текст измерять текущими font/language/DPI. Рефакторинг,
  новая модель/логирование не разрешают пиксельных изменений. Исправление самого
  scroll/layout — отдельное прямое поручение с проверкой обеих ОС.
- До исправления смотреть текущие Geometry/setsGeometry/arrange/PageMechanics
  и Git-историю рабочего механизма. Исторические anchors497841e/75abefd/d84343d
  полезны для сравнения, но не заменяют актуальный контракт. Общие точки:
  src/gui/common, PageMechanics, ControlTheme, NumericFieldTheme, OsdPopup/OsdService.
- Preview не разрешает неподтверждённые EC writes; отсутствующее/retained/requested/
  saved/applied/физически измеренное состояния различаются и в UI, и в журнале.

<a id="r7-build-release"></a>
## R7. Build, portable и release

Правила версии/Resume, обязательной цепочки build → portable →4SHA → commit,
личных файлов и штатного tray exit полностью заданы в корневом AGENTS.md.
Дополнение: при сборке одного GUI target ALL stager мог не выполниться — явно
вызвать общий StagePortableWindows/StagePortableLinux. Не выдавать build/staging
за обновлённую working portable. В body продуктового commit указывать модели,
изменения поведения, runtime/portable, проверки/хэши и границы валидации.

- Архивы/manifest/build-info/SHA256SUMS/GitHub_Releases — только явный запрос;
  общий Package-Releases.py и «Инструкция упаковщику» APP-UPDATES обязательны.
  mctl остаётся в working portable владельца; release GUI-only. Никаких личных
  settings/logs/dumps/source/PDB/SSH keys. Состав/лицензии/fonts/pinned modules,
  archive content, manifest, hashes и provenance проверяются без GUI/hardware запуска.
  Разные BIOS/EC/driver versions не приравнивать из-за одной упаковки.
- Publish/push/tag/signing отдельно разрешаются пользователем. Локальная упаковка
  не является публикацией. Build number генерирует сборка. Update helper работает
  до GUI/EC/init, сохраняет личные настройки, проверку пакета и normal exit.
  Не ослаблять HVCI/SecureBoot/blocklist и не запускать reader/installer как «тест».
- `build/` предназначен для действующих CMake-сборок, состояния общего билда и
  баз `clangd`; одноразовые прогоны размещать в игнорируемом `out/`, а отчёты и
  доказательства — в `research/` с записью в `research/INDEX.md`. Новые физические
  декомпиляции размещать в общем `../Decompile/`; исходные пакеты и прошивки —
  по назначению в `../Reference_Projects/`, `../Control_Center/` или `../BIOS_EC/`.
  Проверять уже существующие копии и ссылки до переноса. Цель — не более
  десяти каталогов без корневого `CMakeCache.txt`; превышение — сигнал для ревизии,
  а не разрешение удалять содержимое или игнорировать вложенные CMake-сборки.
- Оригиналы прошивок, декомпиляции, пользовательские логи и настройки сохранять.
  При перемещении проверять зависимости и содержимое, обновлять действующие пути;
  не заменять физический перенос созданием junction или скрытой ссылки.

<a id="r8-devices"></a>
## R8. Модели и аппаратные контракты

- Перед новой моделью/driver/переносом hardware читать DEVICE-ARCHITECTURE.md.
  Profiles — src/devices, maps/codecs — src/protocols, transport — src/platform.
  Совпадение CPU/имени/Project ID, установленный driver и каталог отчётов не дают
  разрешения raw EC. Экспериментальные OEM candidates — не writable capability.
- До изменения bits/registers читать EcRegister.h (совместимый Wujie facade,
  не универсальная карта), реальный protocol header и контракт функции. Сохранять
  соседние биты, write order, ownership, recovery/rollback и source comments;
  уточнённые назначения фиксировать рядом с кодом и в нормативном контракте.
- До Huawei/Honor читать devices/huawei/hlyl-wxx9/README.md. Typed LaptopControl/
  HWMI/I2EC или Linux sysfs — не Uniwill/EcAccess. Не переносить4/7, FAN RAM tables,
  MECHREVO charging/lighting masks на Huawei/MINISFORUM или другую непроверенную плату.
- Все изменения охватывают реально затронутые модели и обе ОС; OS-specific adapters
  допустимы, чужой hardware transport — нет. Статическая ревизия, successful build,
  readback конфигурации, package hash и физический эффект — разные уровни evidence.
  При изменении поведения обновлять соответствующий контракт. Не возвращать
  отменённую очистку таблиц/сброс при выходе по старому отчёту эксперимента.
