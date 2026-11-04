# Архитектура — открытая работа

Обновлено 01.11.2026 после инкапсуляции SETs/FANs, объединения CPU/APU, подсветки, батареи и FANs. Карта текущих владельцев —
[PROJECT-MAP](PROJECT-MAP.md), правила — [AGENTS](../AGENTS.md) и
[WORKING-RULES](WORKING-RULES.md). Здесь только актуальный backlog; статистика
файлов/срез ревизии — research/repo-ergonomics-audit-20261031/REPORT.txt.

## Закрытые пункты, которые не нужно повторять

- INFO/KEYs/SETs/FANs перенесены из GUI к существующим resource owners/runtime.
- GPU GUI/CLI используют общий admission/provider/readback/result; direct CLI
  setters и page threads удалены. Реальные NVIDIA/Radeon guards сохранены.
- Update Windows/GTK имеет одну модель; OSD HWND loop — core actor/OS binding;
  Sc обеих ОС имеет одну core очередь и имеющиеся native adapters.
- В src/gui нет native thread/jthread/async creators, служебные слои не включают
  GUI headers. Это статическая характеристика кода, не число физических threads
  процесса и не доказательство аппаратной проверки каждой функции.
- Первый пакет навигации: компактный root AGENTS с полной активной детализацией,
  текущие README/MAP, фильтр обычного rg. Evidence не удалён, правила мигрированы
  с таблицей покрытия. Этим документационным пакетом scroll/геометрия не менялись.

## A. Инкапсуляция SETs/FANs — реализована 31.10

- Четыре владельца Windows/Linux хранят backend/очередь/pending/draft/cache приватно.
  GUI использует композицию, Windows наследует только state-free presentation ports.
- GUI связывает const Presentation с данными владельца: без второго cache/copy,
  writable state, queue или backend handles. Присутствие backend — opaque bool/query,
  не permission; актуальный controller status запрашивается у владельца.
- Изменения draft, native band/target, power selection, lifecycle/tray/cycle идут
  командами владельца. Защитные band counts/диапазоны и прежний порядок сохранения
  остались. Stock power intent входит в submit; Linux provider задаётся конструктором,
  seed модели допустим только для preview metadata.
- SettingsBackendsWindows.h/.cpp удалены; factory/order/admission находятся внутри
  приватной SETs implementation. Existing fixture friendship не открывает production
  setters. Фикстуры адаптированы, но не запускались без отдельного запроса.
- Сборки/хэши и сравнения geometry/writers — research/models-encapsulation-20261031.
  Это не physical/runtime тест всех устройств. Следующие пункты ниже остаются открыты.

## B. Укрупнение частных API и компиляционные границы

- Пакет CPU/APU 01.11: frontend/lifecycle writers сведены к executePowerResource;
  StandardPowerModesWindows пара и пять параллельных публичных apply-входов
  удалены. Custom/slots входят туда же, backend guards/rollback остаются у модели.
  Критерии: 0 обходов проверяемых CPU writer boundaries, 0 возвращённых старых API,
  0 дублирующих frontend сохранений GM Custom marker. Это проверяется отдельным
  tests/offline/power-runtime; карта/границы evidence — research/power-single-path-20261101.
  Пакет FANs по этому критерию завершён ниже; физическая validation остаётся отдельной.
- Подсветка 01.11: один request для SETs/CLI/lifecycle/cycle, общий native entry обеих
  ОС, private SingleZone/HID policy; LightingControlLinux.h/.cpp и public apply/
  cycle-file forwarding API удалены. CLI не кодирует RGB/effect самостоятельно,
  GM explicit сохраняется через common completion. Native pair не маскирует ошибку
  первого поля успехом второго. Проверки: общий architecture_audit, portable fake
  Native/HID-data и четыре SingleZone семьи на EC в памяти; research/lighting-single-path-20261101.
- Батарея 01.11: общий request и execution/completion для GUI/CLI/внешнего restore;
  portable native upper/restart/pair policy и storage одинаковы для обеих ОС.
  Четыре отдельных Honor/HLYL policy файла, applyBattery*/raw-pair wrappers и
  второй GUI persistence route удалены. Низ/автосохранение/restore не удалены;
  controller-owned journals, native voltage stages, ROM/aging guards сохранены.
  Проверки/карта/границы — research/battery-single-path-20261101.
- FANs01.11: один request/entry для GUI/CLI/внешнего restore/составного Custom.
  Raw family writers, per-backend workers и EC/draft wrappers удалены, очередь
  содержит общий DTO. Исправлены CLI Manual vs Online, универсальные fixed bounds,
  caller lease и потеря канала в двух Online-командах. Native ownership/readback
  проверяются одинаково. Кривые, codecs, temperatures/guards и internal controllers
  сохранены; research/fans-single-path-20261101. Offline не равен hardware validation.

- Продолжить разбор read-only SettingsPowerSelection/header weight только при реальной общей
  ответственности. SettingsBackends и StandardPowerModes уже поглощены private owners. Public CLI
  persistence и model protocols сохранять; не создавать forwarding headers.
- Облегчить ResourceControl/Monitoring/GpuPresentation headers: stable contracts,
  forward declarations, implementation там, где она не template. Измерить эффект
  на include footprint/incremental compile; rename сам по себе не оптимизация.
- Обновить устаревшие planned/stub comments/status Linux CMake при отдельной
  правке build inputs; навигационный пакет CMake не меняет.
- Согласовать имена владельцев и файлов (FansOperations/FansServices/FanDeviceActions,
  DiscreteGpu/NvidiaGpuPage, INFO/BiosPage) без новых compatibility wrappers.
- Локальные UTF converters имеют разные strict/fallback policy; общая реализация
  должна сохранить их поведение. Никакого слепого объединения по похожим строкам.
- Не ставить произвольную цель уменьшить477файлов до заданного числа. Критерии:
  меньше public state/переходов для типичной правки, удалённые private дубли,
  сохранённые working contracts и доказуемый compile/read footprint.

## C. Данные, оборудование и реально отсутствующие adapters

- Первый пакет надёжности01.11: CLI --read-only проверяется до Runtime; активные
  пары логов имеют OS leases; cache inventory и общий atomic writer защищены от
  concurrent publishers/readers. Проверки — CLI admission и file-only child
  processes Windows/Linux, без аппаратных вызовов. Evidence:
  research/cli-storage-safety-20261101. Матрица моделей/общий test runner из
  последующей ревизии этим пакетом не объявляются завершёнными.
- Проверить coverage каталога/единиц/identity/stale/absence при hotplug, сне и
  смене устройства; не делать Unknown нулём и не называть слот уникальным без proof.
- Native Linux support и hardware parity проверяются по backend/model/driver;
  чужие Windows EC masks не добавляются ради UI. Для GNOME/wlroots Sc display
  writer пока отсутствует; rootless portal binding требует согласия/keymap.
- Подтверждение физических charge-stop/restart и автономного95/90, temperatures
  без PawnIO, полноты SPI/EC и успешного EEPROM capture остаётся отдельной
  device-specific evidence задачей. Старые версии логов не проверяют новый код.
- Полные пользовательские сценарии UI/ошибок/sleep/restore для обеих ОС ещё нужно
  проверять адресно. Unit/fake snapshot не доказывает физическое оборудование.
- Хранение новых release binaries и live portable в Git — отдельная политика
  владельца. Сейчас она не меняется; evidence/history не удалять и Git не переписывать.

## Проверка и завершение пакета

Тесты добавлять/запускать только по явному запросу. Для законченного продуктового
пакета — общий Build-All, обе portable, версии/4SHA, подробный commit. Не делать
Release после каждой мелкой правки и не смешивать чужие изменения. Documentation/
search-only пакет не меняет бинарник и не требует нового продуктового номера.
