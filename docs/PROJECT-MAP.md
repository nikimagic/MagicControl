# MagicControl — маршруты по проекту

Lenovo KWCN: [контракт](LENOVO-KWCN.md), `src/devices/lenovo/legion-kwcn`
(общий Windows owner WMI/FAN/INFO), `src/protocols/lenovo` (чистые codecs и
factory fingerprints), `tests/offline/lenovo` (протокол, Custom и startup/resume).

Карта кода обновлена 01.11.2026: инкапсуляция SETs/FANs, общие CPU/APU, lighting, battery и FAN-входы; номер и hashes сверенной сборки —
в [README](../README.md#проверенный-срез) и build/product-build.json. Старые
датированные отчёты относятся к указанному в них commit/устройству, не к текущему
status. Эта карта — навигация, не допуск к железу и не повтор нормативных правил.
Перед изменением поведения — [AGENTS](../AGENTS.md) и [WORKING-RULES](WORKING-RULES.md).

## Структура

| Каталог | Назначение |
|---|---|
| src/core | ResourceControl/DeviceOperation, state/catalog, contracts, preferences, audit/history |
| src/devices | Точная модель и profile/controller; registry и reported profiles |
| src/protocols | Карты/кодеки/допустимые маски без renderer и OS widgets |
| src/platform | Windows ACPI/HWMI/I2EC/PawnIO и Linux sysfs/desktop adapters, collectors |
| src/gui | Общие data presentation/geometry/theme плюс Win32/GTK bindings |
| src/cli | Синхронные запросы тех же операций, SSH lifetime и JSON output |
| src/update | Verified check/prepare/helper и общая модель update::Operations |
| cmake, Tools | Build/embedded runtime/staging и единственный packager |
| docs | Текущие тематические контракты и явно датированные исторические ревизии |
| research, Users_logs, archive | Evidence/дампы/декомпил/предыдущий код; адресный поиск с --no-ignore |
| MagicControl, GitHub_Releases | Личная working portable и локальные пакеты; не источник чистых defaults |

`.ignore` ограничивает обычный rg, но не меняет Git/build и не удаляет evidence.
Чистые settings packages — `packaging`; actual logs привязаны к реальной плате,
не к выбранному в меню preview. Системный Current Working Directory — MagicControl.

## Вход → владелец → факт → сохранение

Пути ниже относительны src; функции/классы — быстрые символы для `rg -n`.

| Домен | Вход и владелец | Backend/readback | Persistence/view |
|---|---|---|---|
| INFO inventory | GUI BiosPage/CLI → InfoOperation / executeInfoInventoryResource, core/include/MagicControl/InfoResourceControl.h и platform/common/InfoDumpResource.cpp | HardwareInventory + native OS providers; cancel до save | InventoryCache с OS read/publication lease → FileStorage/существующий storage leaf; bios-inventory.json в logs_<плата>; GUI poll/CLI JSON |
| EC/BIOS/VBIOS/PowerInput | InfoOperation → executeInfoDumpResource → typed collector | common DiagnosticDump/BiosRomDump/VbiosRomDump/PowerInputDump; reader сам проверяет support/range/identity/размер/содержимое | Один audit/transcript/result, whole/partial/failed/cancelled; GUI не сохраняет повторно |
| PawnIO installer | InfoOperationsWindows → verified prepare → UI launch → completion | Явный официальный мастер, статус/версия после выхода | Сырой callback audit + единственный INFO terminal result; Linux адаптера нет |
| KEYs explicit | KeysPage/CLI → KeysOperations, core/include/MagicControl/KeyResourceControl.h и core/src/KeysLifecycle.cpp | Windows KeysHardwareWindows, Linux KeysControlLinux → общий typed setter и fresh observation | completeKeyResource → KeysPreferences/keys-settings.ini; UI take/OSD после overlays |
| KEYs startup/resume/observe | KeysLifecycle под тем же владельцем | Те же readers/setters, собственная retry/suspend/Bluetooth policy | Один writer; внешние более свежие строки не затираются baseline |
| SETs Windows | SettingsPage → typed SettingsAction → SettingsOperationsWindows / ResourceDispatch | SettingsActionWindows → BatterySettings/CustomMode(executePowerResource)/LightingControl(executeLightingResource)/EcLogger → native controller | SettingsSnapshot freshness/confirmation → SettingsPersistence/нативный writer; GUI accepted state |
| SETs Linux | SettingsPage → SettingsOperationsLinux в SettingsServicesLinux | SettingsWorkerLinux/LightingControlLinux → поддержанный LaptopControl/sysfs; own sleep epoch | Common model result/cache и backend persistence; неподключённый writer остаётся unavailable |
| FANs Windows | FansPage → FansOperationsWindows в FanDeviceActionsWindows | FanWriteRequest/executeFanResource → exact FanController/GM/NativeFixedFans; общий command/readback | Model queue с тем же DTO; caller lease, pending/confirmed; storage и receipts у прежних owners |
| FANs Linux | FansPage → FansOperationsLinux в FansServicesLinux | MonitoringReader и только имеющиеся typed backend возможности | Прежние profile storage/result/cache; без чужого Windows EC writer |
| Lighting GUI/CLI/restore | LightingWriteRequest → executeLightingResource; Windows policy в LightingControlWindows, native обеих ОС в LaptopLightingResource | Проверенный SingleZone/native/HID backend; cycle Update не обходит expected/epoch | Explicit fresh observation → rememberLightingChoice; native/HID persistence у backend; временный restore не новый выбор |
| Battery GUI/CLI/restore | BatteryWriteRequest → executeBatteryResource; Windows owner в BatterySettingsWindows, portable native policy в LaptopChargeResource | Existing exact EC/native controller/core aging; native transaction и setter readback | Один observed-pair completion Wujie; native persistence у owner; shared lower/capacity store; GUI battery save path удалён |
| GPU GUI/CLI | GpuOperations / executeGpuOperation в platform/common/DiscreteGpuOperation | Общая DeviceOperation reservation → исходный DiscreteGpu provider ID → NVIDIA/Radeon range/readback | Общий result/audit/cache, GUI только controls/readiness/OSD; direct CLI setters отсутствуют |
| Update | Оба UpdateWindow → update::Operations, AppUpdate.h/AppUpdate.cpp | check/prepareAndLaunch → проверяемый пакет и OS helper, stop/progress | Общий OperationState; UpdatePresentation переводит только на UI thread; обычный exit |
| OSD | Message payload → OsdPopup (Windows) / OsdService (GTK) | WindowEventLoop/core actor с native HWND affinity; GTK/GDBus native dispatch | Существующие State/render/geometry, команда не создаёт новый executor |
| Sc | Windows OEM208 или Linux portal/GTK → core/include/MagicControl/DisplayRotationControl.h | WindowsDisplay CCD либо DisplayControlLinux KDE/KScreen/X11; +180° и readback | Один parity/token/result owner; native delivery, OSD, журнал |

Для CLI аппаратная запись также требует process ownership; Windows mutex и Linux
flock не подменяются instance-local provider mutex. Read-only CLI остаётся отдельным
синхронным потребителем. Наличие resource key/preview/cache не разрешает запись.
Исторические planned/stub строки в Linux CMake не являются текущей матрицей
поддержки: сверять target sources и реальные adapters.

## Общие владельцы

`ResourceControl.h` содержит finite Runtime и registry постоянных actors.
SETs/FANs GUI используют composed model и const Presentation bindings; backend,
queue/pending и изменяемые drafts принадлежат private owner. Windows View/ViewPort
содержит только native presentation callbacks; worker их не вызывает. Из GUI нет
доступа к backend handles или mutable model state. Private backend factory SETs
поглощена SettingsOperationsWindows; старые файлы удалены.

ResourceWorker/ResourceDispatch — handles/typed queue; ResourceActor — lease для
длительных control/event loops с прежним stop/join. UI threads не создают own
worker. Исключения OS lifetime — reader reapers/installer-exit observation,
а не дополнительные application command paths. Audit TaskContext изолирует TLS.

ResourceState принимает наблюдение; ResourcePresentationView относится к его
поколению. Retained display data сохраняет исходный timestamp и не служит свежим
control sensor. Unknown/zero/absent/stale/requested/saved/applied различаются.
Для FAN/thermal marker отображаемая GPU не меняет физический канал вентилятора.

## Модели и допуски

[DeviceRegistry](../src/devices/DeviceRegistry.cpp) собирает17 известных профилей
и отдельный Unknown; reported entries —
[ReportedProfiles](../src/devices/mechrevo/ReportedProfiles.cpp). Сходство названия,
CPU, Project ID или installed driver не является совместимостью регистров.

Собственные Windows controllers/политики имеют Wujie, GM6IR0C, GM5HG0A, L16,
HYDROC, Honor HLYL и HPBSD; набор функций и runtime admission различаются.
Остальные reported profiles не получают model-specific EC control из каталога;
системные KEYs/INFO/фактический GPU оцениваются независимо. Подробности и evidence —
[DEVICE-SUPPORT-MATRIX](DEVICE-SUPPORT-MATRIX.md), [DEVICE-ARCHITECTURE](DEVICE-ARCHITECTURE.md).
Huawei/Honor и MINISFORUM не используют Wujie raw map.

Windows firmware selection и support data — BiosSupport.h, BiosRomDump и native
AFU/FPT/Insyde adapters. Linux не получает Windows readers; NVFlash имеет отдельный
ELF adapter. Версии payload сверять по текущему embedded runtime/collector, а не
по старой таблице README. Ordinary VBIOS/EEPROM, validity/physical completeness
и recovery capability не приравниваются. См. [VBIOS-DUMP](VBIOS-DUMP.md).

## Контракты, которые читать адресно

| Изменение | Основной источник |
|---|---|
| FAN ownership, Default, APP, native/online/exit | [FAN_MODES](../src/platform/windows/FAN_MODES.md) |
| CPU stock/Custom/SMU,783–785 | [POWER_MODES](../src/core/src/POWER_MODES.md) |
| Battery charge/native/aging/USB-C/restore | [BATTERY](../src/platform/windows/BATTERY.md) |
| KEYs и отдельный файл настроек | [KEYS-PERSISTENCE](KEYS-PERSISTENCE.md) |
| Общие resource entrypoints | [CONTROL-RESOURCE-ENTRYPOINTS](CONTROL-RESOURCE-ENTRYPOINTS.md) |
| Каталог/данные/readback semantics | [RESOURCE-ARCHITECTURE-PLAN](RESOURCE-ARCHITECTURE-PLAN.md) |
| UI geometry/header/scroll | [LINUX-UI-PARITY](LINUX-UI-PARITY.md), Geometry/ScrollModel/PageMechanics |
| Readers и диагностический результат | [DIAGNOSTIC-DUMPS](DIAGNOSTIC-DUMPS.md), [VBIOS-DUMP](VBIOS-DUMP.md) |
| Sc/portal/compositor/keymap | [LINUX-SC](LINUX-SC.txt) |
| Build, installer/update, архивы, лицензии | [APP-UPDATES](APP-UPDATES.md), [CLI](CLI.md) |

## Свежие изменения и границы проверки

Сверенный код включает common GUI/CLI/runtime cutover, Linux Sc и последующие
Wujie aging/thermal-history изменения. Общая visibility policy скрывает USB-C
Battery/Performance, raw EC policy и Logger controls; сохранённые данные/API не
удалены. USB-C priority не поддержан: admission отказывает до writer/save и не
replay-ит этот режим при startup/resume/adapter change. BatteryAging находится в
core; charge/aging идут через BatteryWrite и native transport. Заводская дата и
циклы не заменяются альтернативной датой. Неподтверждённый override readback не
показывается применённым. Основание отказа USB-C и текущие masks/identity — в BATTERY и codec.

Identity startup до auto-apply и append-only histories — RomIdHistory;
BMS manufacture-age evidence — BatteryIdentityHistory; battery journal ownership
не подменяется GUI/CLI independent files. Логи/inventory принадлежат реальной
плате; постоянная история не ротируется вместе с тремя парами session logs.

GUI execution перенос завершён в коде для перечисленных доменов: прежний список
GPU/Update/OSD/Sc в WORKLIST закрыт. Это не физическая проверка всех плат и ОС.
Остаются дополнительные private API/header weight и device/compositor validation — [актуальный WORKLIST](ARCHITECTURE-WORKLIST.md).
Старые развёрнутые README/MAP и сведения их срезов сохранены в Git до этого пакета;
датированные research/REVISION — история с собственной базой, не новая инструкция.
