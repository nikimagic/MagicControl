# Состояние проекта: архитектура, сложность, оптимизация и тесты

30.10.2026. Проверенный срез: f4d96956, продукт 0.1.407. Сравнение исходников
с f17042fd/0.1.401. Код, настройки, сборка и оборудование не изменялись.
Новые тесты не запускались; результаты исполнения ниже взяты из сохранённых
отчётов и журналов с явно указанным возрастом.

## Оценка

Движение к лучшему подтверждается кодом, а не только переименованиями файлов.
Есть рабочая архитектура с типизированными доменными владельцами и общими
входами операций. Технический долг остаётся значительным; после последних
переносов проверка поведения отстаёт от структурных изменений. Компиляция
Windows/Linux не является доказательством отсутствия регрессий управления.

## Измеримое изменение

Счёт: собственные .cpp/.h/.hpp под src, без runtime/third_party и generated
translation data. Строки включают комментарии/пустые строки; это не метрика
цикломатической сложности и не оценка покрытия тестами.

| Показатель | 0.1.401 | 0.1.407 |
|---|---:|---:|
| C++ файлы | 471 | 475 |
| C++ строки всего | 55 676 | 56 291 |
| GUI строки | 17 792 | 16 543 |
| Windows FansPage.cpp | 2 020 | 1 499 |
| Windows SettingsPage.cpp | 1 899 | 1 501 |

Текущий статический поиск: GUI thread creators=0; прямые include GUI-заголовков
из core/platform/devices/protocols=0. Это улучшение границ, но не доказательство
эквивалентности всех ветвей выполнения. Полные числа — CODE-METRICS.json.

## Архитектура и пути выполнения

Основной поток: native view -> application/domain Operations -> typed resource
entry -> прежний board controller/protocol -> OS transport. Обратный поток —
подтверждённый результат/наблюдение -> принятый snapshot/view -> renderer.

- Существуют реальные владельцы состояния/заданий: SettingsOperations,
  FansOperations, KeysOperations, InfoOperation, GpuOperations, update::Operations,
  DisplayRotationOperations. GUI передаёт действия и показывает результаты.
- Общий ResourceWorker runtime имеет четыре потока, один только для Interactive.
  Постоянные controller/watchdog/event loops имеют отдельные ResourceActor
  leases и не занимают пул конечных заданий. Много потоков или backend-адаптеров
  не означает много реализаций одной команды.
- Перепроверены прежние шесть находок: CLI slot преобразован в 0/1/2; Wujie CLI
  использует rememberSettingsChoice; restore использует общие power/lighting
  entries; GPU GUI/CLI используют executeGpuOperation и admission; Linux GUI/CLI
  держат общий flock; capture признаёт 60 через общую BatteryUiPolicy. Это
  подтверждение исходниками, не новый hardware test.
- ResourceState остаётся принятым состоянием потребителя, не универсальной
  process-wide базой всех датчиков. Разные DTO/cache/views не являются вторыми
  аппаратными владельцами, но единый telemetry collector для всей машины
  ещё не появился.
- DeviceProfile содержит точные model-ID helpers и семейные ветвления;
  GM controller объединяет несколько плат и FAN/power/keyboard/lifecycle.
  Добавление новых семейств всё ещё требует соответствующего кода и transport
  admission. Утверждать «любая новая плата подключается только данными» нельзя.

## Сложность и файлы

Самые крупные реализации: MainWindow 1887 строк, Wujie FanController 1697,
GM controller 1583, SettingsPage 1501, FansPage 1499, DiagnosticDump 874.
Размер сам по себе не доказывает плохое устройство, но такие сочетания
обязанностей дороги для проверки и изменений. Особенно важны явные границы
state machine, hardware codec, lifecycle policy и orchestration.

В собственном коде 902 строки длиннее 200 символов;516 из них в platform.
Часть — сообщения/JSON, часть — компактные ветви. Это показатель плотности
текста, не точная сложность функций и не доказанный runtime bottleneck.
113 файлов имеют не более 20 строк: среди них публичные контракты, declarations,
forwarding headers. Их нельзя автоматически объявить мусором.

Смысл разделения обязанностей улучшился, общий объём вырос на 615 строк.
Удаление строк/файлов не должно становиться самоцелью. Оставшийся Windows
DiscreteSlider используется тестом native_controls; прежнее замечание о двух
совершенно неиспользуемых headers было уточнено в follow-up. Linux header удалён.

## Производительность

Уже имеются разумные механизмы: busy guards, отсутствие thread-per-page,
background FAN polling с 5 с ограничением, лёгкий keyboard-idle poll1 с вместо
полного SETs read чаще 3 с, disk cache5 с, memory metadata cache1 мин, sleep epochs,
retained readings с исходным временем. Они уменьшают лишнюю работу и UI blocking.

Незакрытые вопросы:
- FANs и SETs имеют свои MonitoringReader/ApuPowerReader экземпляры; их
  локальные кэши не объединяют все обращения к одинаковым источникам.
- ResourceState/легенды/строки и перерисовка обновляются с опросом; цена этого
  не измерена. Предположение, что линейный поиск по небольшой route table или
  копирование десятка observation rows существенно грузит CPU/GPU, не доказано.
- Runtime имеет ограниченное число потоков, но сами очереди не имеют capacity
  limit/cancel-by-job API. Длительные неотменяемые WMI/SDK calls требуют проверки
  влияния на другие observations и закрытие клиентов. Reserved interactive
  worker защищает часть responsiveness, но не прерывает уже выполняющийся RPC.
- Реализация общего runtime находится в публичном ResourceControl.h; её изменения
  распространяются на много translation units и увеличивают стоимость сборки.

Профилирования CPU/RAM/latency для 0.1.407 в этом задании нет. В доступном сеансе
работающий MagicControl для замера не наблюдался; программу не запускали, потому
что её старт восстанавливает аппаратные настройки. Называть продукт быстрым или
медленным по одному чтению исходников нельзя. Нужны idle/visible/background,
командные p50/p95, queue wait, число обращений к источникам, handles и рост памяти.
Presentation cache нельзя использовать как feedback охлаждающего контура ради
сокращения опроса: это изменило бы аппаратное поведение.

## Тесты и качество выпуска

Тестовый код существует и содержателен:43 C++ файла в tests и tests/offline,
около 7.4 тыс строк вместе с test helpers/scripts; research test_util исключён.
В CMake47 add_test declarations, с OS conditions и foreach expansion; это
не число выполненных тестов. Есть fake backend checks, battery decoding,
readback/rollback/admission, worker failure, sleep epochs, resource projection,
geometry/fonts и file-only firmware validation. Основные checks не зависят от
исчезающих Release assert. См.TEST-INVENTORY.json.

Недостатки процесса:
- VERIFICATION для 0.1.403/404/405/406/407 отмечает tests_run=false.
  Build-All явно передаёт MAGICCONTROL_BUILD_TESTS=OFF. Отдельного прохода
  нового состава после рефакторинга в просмотренных отчётах не подтверждено.
- Найденный свежий LastTest.log settings-queue-review —29.10; основной Linux
  LastTest.log —13.10. Они не подтверждают сегодняшнюю 0.1.407.
- Есть базовые ResourceWorker tests, но не найден отдельный сквозной тест
  насыщения общего пула/ResourceActor lifecycle, новых GpuOperations и
  DisplayRotationOperations. CLI parser -> общий request -> writes/persistence
  parity также нуждается в проверке, а не только source-name regex.
- Architecture guard полезен против возврата известных обходов, но в основном
  проверяет текст/число определений. Семантику индексов, epochs, cancellation
  и фактическое сохранение он не доказывает.
- В репозитории не найден проверочный CI workflow и конфигурация coverage/
  sanitizer runs. Это не утверждение об отсутствии любой внешней CI-системы.
  Процент покрытия неизвестен. MAGICCONTROL_WERROR выключен, предупреждения
  компилятора не блокируют обычный release build.

Четыре GUI/CLI бинарника обеих ОС собраны одной версией и имеют verification
manifests. Прослеживаемость версии, build inputs и SHA-256 — сильная сторона
поставки, отдельная от correctness/runtime validation.

## Следующий полезный этап

1. Регрессионный проход текущего среза на hardware-free fixtures Windows/Linux;
   отдельно обозначить GUI/admin/private-fixture/hardware suites и skips.
2. Проверить новые runtime/ownership/GUI-CLI-restore контракты на отказах,
   насыщении, отмене и сне. Не смешивать настройку power с KEYs или менять кривые.
3. Снять performance baseline, затем устранять доказанные повторные чтения,
   задержки и рост ресурсов. Упростить самые рискованные state machines и
   вынести runtime implementation из публичного header при сохранении API.

Обновлять документацию вместе с кодом: ARCHITECTURE-WORKLIST всё ещё перечисляет
GPU/Update/OSD/rotation как оставшийся перенос, хотя 0.1.404–407 уже изменили эти
пути. Оценка проекта должна опираться на текущий код и проверенный результат.

