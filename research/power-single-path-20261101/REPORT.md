# CPU/APU: один вход выполнения

База: `1b9e25b8`, общий продукт 0.1.423. Пакет от 01.11.2026.
CI, лицензия и хранение бинарников отложены владельцем. Аппаратные запуски,
чтение/запись реального EC/SMU/прошивок и запуск GUI/CLI не выполняются.

## Что найдено и изменено

1. Штатные CPU-профили имели отдельную публичную цепочку StandardPowerModesWindows,
   а Custom/Auto/Allowed — executePowerResource. CLI и Wujie restore выбирали между
   ними сами. Теперь все внешние CPU-запросы входят в executePowerResource.
2. CustomEditor/Preset были ещё двумя внешними apply-входами с повторными guards
   и вложенным повторным dispatch. Их тела стали приватными handlers одного входа.
3. SettingsPowerContext и отдельный callback получения power-контекста удалены:
   Stock и Custom используют один CustomModeContext существующего owner.
4. Stock-only WM_COMMAND guard молча игнорировал Minimal0 и Auto4. Убрана только
   эта ошибочная классификация; доступность/повторный допуск устройства сохранены.
5. CLI GM парсил все Custom поля до250, независимо от модели. Теперь он использует
   те же customFieldMinimum/Maximum, что редактор; backend по-прежнему валидирует
   реальный диапазон, порядок, codec, питание и identity. HYDROC PL4 не срезается до250.
6. GM setter уже сохранял снятие Custom active; GUI повторял этот writer.
   Повтор удалён. Non-GM explicit Stock/Minimal/Auto снимает intent после checked
   setter; Wujie CLI теперь получает то же завершение, что SETs.
7. У restore отдельный origin: воспроизведение состояния не является новым выбором.
   Его legacy Wujie performance branch больше не вызывает FanController напрямую.
8. Снятие отсутствующего Custom active не загружает/создаёт библиотеку FAN-кривых:
   отсутствие библиотеки уже означает отсутствие активного Custom. Это устраняет
   отказ штатной CPU-команды из-за отсутствующего, не относящегося к ней CSV.

Удалены StandardPowerModesWindows.h/.cpp и неиспользуемый Gm6Controller.setMode.
Убраны пять публичных apply-входов: applyStandardPowerProfile, applySettingsPowerButton,
applyStockPowerAndDeactivateCustom, applyCustomManual и applyCustomPreset.
Три Wujie policy writers перенесены из общего header в private implementation.
Новых производственных файлов, очередей, потоков и backend wrappers нет.

## Карта путей

| Вход | Декодирование | Общий CPU-вход | Завершение |
|---|---|---|---|
| SETs stock/Minimal/Auto | SettingsActionWindows, кнопка0/1..3/4 | executePowerResource | прежний SettingsExecution: observation/classification/remember/result |
| SETs Custom | подготовка редактора без I/O; CustomEditor request | executePowerResource | checked model writer, прежний draft save, затем observation/result |
| SETs slot1/2/3 | slot0/1/2 у owner | executePowerResource(Preset) | прежний составной FAN/CPU apply, library selection/result |
| CLI stock/Minimal/Auto/Custom/GM slot | CurrentCommands: аргументы, внешний slot-1 | executePowerResource | model-owned persistence; Wujie fresh snapshot/rememberSettingsChoice |
| Wujie startup/resume, включая legacy | ConfigurationRestore, origin Restore | executePowerResource | прежний restore step, epoch/cancel/error handling |
| GM внутренний replay | тот же controller, те же сохранённые данные | его же checked setters | тот же journal/readback/configuration owner |
| Физическая OEM-кнопка GM | прежний whole-profile cycle | прежний applyNativeProfile у device owner | FAN/CPU/LED/profile, прежние epoch/AC/rollback |

Последние две строки — внутренний model lifecycle и другая whole-profile операция,
а не второй frontend CPU writer. Их нельзя искусственно заменить CPU-only Stock.

## Модели и ОС

| Семейство | Сохранённый реальный путь |
|---|---|
| WUJIE | Stock: FanController performance либо standalone guarded writer; Custom: координированная CPU база и overrides; Auto/Allowed: прежние узкие overrides |
| GM6/GM5/L16/HYDROC | Gm6Controller с собственными identity/default tables/codec/AC/gates/journal; существующие составные Custom/Minimal FAN действия сохраняются |
| Honor HLYL | Native Stock через LaptopControl; Custom через существующий exact SMU policy; не добавлены Uniwill-профили, Auto или неподтверждённый Minimal backend |
| HPBSD и inventory-only | Сохранена недоступность неподключённых CPU-команд; диагностический SMU writer не превращён в штатное управление |
| Linux | Сохранён отказ отсутствующего CPU backend; Windows EC/SMU транспорт не заимствуется. Общий finite runtime и проверка source boundaries собираются/проверяются на обеих ОС |

Применённый запрос/байтовый readback не объявляется измерением реальных ватт,
частоты, FAN RPM или соответствия прошивке на физическом ноутбуке.

## Проверки и критерии

- Существующий общий `tests/architecture_audit.cmake` расширен: список CPU writers
  допускается только в общем входе, protocol implementation и существующих device
  owners. Старые API отсутствуют, CLI slots и restore origin сохранены. Это
  регрессионный source guard, а не полный C++ call graph или аппаратное доказательство.
- `test_power_entry.cpp`: настоящий SettingsAction/executePowerResource с fake
  LaptopControl; GUI/CLI/restore indexing, no-new-choice при restore, отказ питания/
  readback/unsupported mode без сохранения успеха, slot admission до backend/файлов,
  отсутствие write-permission из одной записи каталога, пустая Custom библиотека.
- `test_runtime.cpp`: настоящий Runtime/ResourceActor/ActionLog.cpp с no-op audit
  sink. Три фоновых работы удерживаются барьерами, четвёртая observation ждёт,
  interactive выполняется; четыре разных finite threads; actor работает при полном
  насыщении finite pool; audit input/TLS очищается после исключения; captures
  освобождаются до публикации completion. Произвольных sleep для синхронизации нет.
- Адаптированная существующая settings_queue fixture отдельно компилируется,
  не добавлена в исполняемый набор этого пакета.
- Сравнение geometry/scroll/renderer и исходных device/protocol bodies фиксируется
  в SOURCE-OBSERVATIONS.json. Итоги сборок/тестов/portable — VERIFICATION.json.

Разрешение на добавление/запуск offline fixtures получено отдельно после отказа
автоматической проверки. Только fixtures; GUI/CLI, драйверы, OEM readers и реальные
аппаратные операции не запускались. Остальные домены, CI, лицензия и Git policy
этим пакетом не закрываются.

## Итог пакета

Общий Release 0.1.424 завершён; Windows/Linux GUI+mctl обновлены в рабочих
portable и независимо сверены по четырём SHA-256, sidecars и Windows VERSIONINFO.
Windows offline:3/3, Linux offline:2/2; общий audit проверяет15 существующих
write definitions и CPU boundaries. Это не число физических write-команд.
Идентичность renderer/scroll/низкоуровневых алгоритмов подтверждена16 source
сравнениями; production .cpp/.h сокращены на60 строк, удалены два файла,
новых production файлов нет. CI, лицензия, история/хранение Git и архивы не менялись.
