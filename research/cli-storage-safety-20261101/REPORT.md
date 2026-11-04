# Первый пакет: CLI admission, log slots, inventory publication

База 481de438 /0.1.427. Все модели и обе ОС: изменения в общем parser/core/storage,
без новых model/transport mappings. Приложение, hardware/EC/SMU/HID/driver/vendor
tools не запускаются. GUI/geometry/scroll и device/protocol sources не меняются.

## Изменения

- Один pure CLI parser/classifier перенесён в существующий CurrentCommands.h.
  --read-only отклоняет control intent/FAN probes независимо от --yes и порядка
  флагов. Вызов до Runtime/SMBIOS/COM/logging/backend init. Ordinary read commands
  и файловые диагностические результаты разрешены. Прежние dry-run и setters
  не дублируются. CLI initialization failure сообщает причину до body команды.
- Logger сохраняет три пары N=1..3 без .part. Выбор/legacy migration под общим
  межпроцессным rotation lease, active slot lease держится до закрытия streams.
  Старейшая занятая пара пропускается; три занятых пары -> false/error без trunc
  или четвёртой пары. GUI retains existing initialization-return handling; CLI
  сообщает отказ. Lock metadata в .locks, стабильные файлы не удаляются.
- FileStorage.h — один узкий контракт в существующем OS storage leaf. Windows
  LockFileEx/CloseHandle, Linux flock/O_CLOEXEC; обычные файлы без symlink/hardlink
  подмены. Нет worker, устройства, глобального hardware lease или GUI зависимости.
- Common file writer: per-target publication lease, unique exclusive sibling
  temporary, checked write/flush/close, существующая atomic replacement. Failure
  удаляет только собственный temporary. Чужой/оставшийся старый .tmp не трогается.
  PortableSettings использует тот же leaf; старый writer удалён, схема/validators
  настроек и read/modify/write policy не менялись.
- InventoryCache GUI/CLI readers/publishers держат общий отдельный lease, включая
  legacy import. Читатель видит целый JSON; publish order — завершение публикации,
  не предположение по wall clock. Legacy source сохраняется. Превышение reader
  limit2MiB отклоняется до замены рабочего cache; read errors не принимаются.

## Проверки

Pure CLI fixture не линкует application/platform/backend:473 admission checks,
включая control families, --yes/--no-ec, flag positions, ordinary reads/aliases.

File-only fixture использует реальные Logging/InventoryCache/ActionLog и OS file
adapters, software board/path stubs. Python запускает только этот fixture, в
уникальном Unicode scratch каталоге. Проверены:

- Три concurrent log owners, отказ четвёртому; активный oldest slot не trunc.
- Повторное использование освобождённого слота, принудительное завершение ТОЛЬКО
  fixture process, освобождение OS leases без удаления стабильных lock files.
- Session совпадает внутри каждой пары; ровно шесть JSONL, без .part.
- Три concurrent inventory writers и два readers; целые согласованные snapshots.
- Заблокированный inventory publisher, восстановление после fixture process exit.
- Oversized snapshot не заменяет предыдущий valid cache.
- Параллельные common atomic publishers, failure cleanup, preservation чужого .tmp.
- Прежний paired_log_slots fixture:74 проверки порядка/metadata/legacy/8MiB+files.

Первый Windows concurrency test воспроизвёл ERROR_ACCESS_DENIED при simultaneous
MoveFileEx publishers. Добавление общего per-target publication lease устранило
ошибку; исправление затем проверяется на обеих ОС. Это filesystem test, не
разрешение на запуск пользовательского приложения, служб или firmware tools.

SOURCE-OBSERVATIONS.json фиксирует сравнения с baseline. Версия, общий build,
обе working portable GUI+mctl и четыре SHA-256 — в VERIFICATION.json.
Foreign runtime settings/inventory/logs/evidence не включаются в коммит.
Архивы, GitHub publication/push/tags, матрица всех моделей и общий test runner
остаются вне этого пакета.

## Завершение

Targeted tests: Windows3/3 и Linux3/3. Existing regressions: Windows8 behavioral
cases и Linux5 прошли; source audit первоначально использовал перемещённый
`bool writer(` как конец KEYs-секции. Обновлён только test endpoint на `int run(`,
добавлена проверка раннего pure CLI admission; этот единственный failed test
повторно прошёл на обеих ОС. Итого проверены9 existing cases Windows и6 Linux.

Общий Build-All Release **0.1.428**, manifest complete, обе платформы verified.
Обе working portable GUI+mctl обновлены штатными stagers. Для всех четырёх
executable совпали build/portable/manifest SHA-256; версии обоих Windows PE и
двух portable sidecars также сверены чтением файлов. Приложение не запускалось.
Список файлов/хэши и граница тестирования — VERIFICATION.json.
