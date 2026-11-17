# Архитектура — открытая работа

Сверено **17.11.2026** по исходникам и манифесту рабочего билда0.1.491.
Это текущий backlog. Владельцы и пути — [PROJECT-MAP](PROJECT-MAP.md),
обязательные правила — [AGENTS](../AGENTS.md) и [WORKING-RULES](WORKING-RULES.md).
Исторические отчёты в research описывают собственную базу и не заменяют код.

## Реализовано в коде

- INFO/KEYs/SETs/FANs/GPU/Update/Sc используют существующих ресурсных владельцев.
  В GUI нет собственных thread/jthread/async creators; native delivery/render
  остаются во frontend. Постоянные контуры используют ResourceActor.
- SETs/FANs инкапсулированы: private backend/очередь/draft/cache, const Presentation,
  typed commands и completion. Удалённые SettingsBackends/InfoWorker/старые
  coordinator wrappers не возвращать ради совместимости имён.
- CPU, battery, lighting и FAN frontend/lifecycle команды имеют общие входы.
  Device backend сохраняет identity/ownership/masks/order/readback/rollback.
  Различия диагностического чтения и платформенных codecs не считать дублями writers.
- Custom един: редактор, библиотека слотов1/2/3, собственные кривые и power options.
  DeviceProfile.customPreset и fanPointCounts — данные, не разрешение аппаратной
  записи. Все16 ячеек сохраняются; активные native точки задаются профилем.
- Общий ConfigurationRestore обслуживает EC и native owners. Новая команда/
  sleep epoch отменяет старое намерение; AwakeDeadline учитывает время бодрствования.
- Lenovo KWCN включён в общие FAN/Custom/INFO/restore пути. WMI ranges/packets,
  ownership journal и Full Speed остаются у exact backend. Контракт —
  [LENOVO-KWCN](LENOVO-KWCN.md).
- BIOS/VBIOS используют TelemetryGate и firmware ticket: pause/drain/reader
  lifetime, отказ при активном safety control. Сохранение обычного VBIOS,
  EEPROM, partial/cancelled результата и физическая полнота различаются.
- CLI --read-only проверяется до Runtime; активные три пары логов и inventory
  publication имеют file leases. Общий atomic writer не использует один .tmp.
- Добавлены preview/Custom/memory tests по реестру, fake EC/SMU/sleep и firmware
  gate/lifetime сценарии. Их наличие не означает свежий успешный прогон всего
  набора или физическую проверку всех профилей.
- Политика Git уже source-only: код/конфигурация/текст. Portable/runtime/assets,
  релизы, логи и бинарное evidence локальны; это больше не отложенное решение.
  Переносы рабочих материалов продолжаются по индексам, без удаления оригиналов.

## A. Текущие функциональные проверки

- **Sc на Wujie:** владелец сообщает об утрате поворота. В исходниках остаются
  AcpiTest_EventULong208 → MainWindow → DisplayRotationOperations → Windows
  display adapter. В просмотренной сессии0.1.491 от17.11 подписка успешна,
  событие208 не найдено. Нужна привязка одного фактического нажатия к свежему
  логу; не заменять событие другим кодом и не поднимать APP/EC по предположению.
- TelemetryGate процессный. Отдельные GUI/CLI/внешние программы им не управляются.
  Проверить NVIDIA provider teardown и владение ещё живым child, включая cleanup
  драйвера после позднего выхода. Актуальные границы — DIAGNOSTIC-DUMPS.
- Разделять общий preview/UI и фактическую поддержку backend. Linux FAN writer
  остаётся unavailable; GNOME/wlroots Sc writer не подключён. Не переносить
  Windows EC masks ради паритета интерфейса.
- Физические charge-stop/restart, aging, температуры, текущая DDR speed и
  полнота SPI/EEPROM требуют device-specific evidence. Старый лог не проверяет
  новую версию. Factory FAN observation не превращать в writable Manual draft.

## B. Покрытие и организация тестов

- Старый FAN negative test в tests/offline/power-runtime повторяет пустой context
  при переборе профилей. Это одна проверка отсутствия backend, не18 разных
  backend tests. Дополнить проверку actual adapter/identity/capability selection.
- Проверить единую воспроизводимую команду автономного прогона и согласованную
  регистрацию наборов. Есть root tests, tests/offline, firmware, lenovo и отдельный
  power-runtime; явные model/OS/timeouts/labels и пропуски локальных fixtures
  должны быть видны. Сохранённые LastTestsFailed не считать текущим результатом.
- Проверять latest intent/readback/persistence/OSD при partial failure, смене
  питания, сне, отмене и позднем completion. Дисплейный cache сохраняет возраст
  и identity, не выдаёт capability или разрешение на запись.
- Свежий clone требует внешних артефактов. Сверять provenance/SHA-256 из
  EXTERNAL-ARTIFACTS, не восстанавливать неподтверждённую версию автоматически.

## C. Сопровождение кода и документов

- Облегчать ResourceControl/Monitoring/GpuPresentation/private headers по измеренному
  include/compile footprint. Не создавать forwarding wrappers и не задавать
  произвольное целевое число файлов вместо уменьшения ответственности.
- Принимать частные API только с общей ответственностью. UTF converters сохраняют
  разные strict/fallback policies; похожий текст не доказывает идентичное поведение.
- Исторические planned/stub comments Linux CMake сверять с реальными target sources.
  Изменение CMake/production inputs выполнять отдельным кодовым пакетом.
- Тематические DEVICE-ARCHITECTURE/POWER_MODES/BATTERY содержат хронологические
  разделы. Последняя уточнённая семантика и действующий код имеют приоритет;
  исторические доказательства не переписывать как результаты сегодняшнего прогона.
- Продолжить проверенные физические переносы build/out/research/Decompile по
  research/INDEX и каталогу материалов. Отсутствие CMakeCache не разрешает удаление.

## Проверка и завершение пакета

Тесты добавлять/запускать по явному запросу. Продуктовый пакет — общий Build-All,
обе portable, версии/4SHA и отдельный локальный commit исходников/конфигурации/
документации. Бинарные результаты в Git не включать. Для docs-only пакета новый
билд и замена portable не нужны. Чужие изменения, настройки и evidence сохранять.
