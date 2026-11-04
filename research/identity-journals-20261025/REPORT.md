# Inventory и постоянная история EC,25.10.2026/0.1.199

Поручение: хранить bios-inventory.json в папке логов ноутбука, добавить
platform_id рядом с rom_id, дописывать дату/время/версию, начальные значения
до автоприменения и последующие automatic/user запросы, результаты и откаты.

## Файлы

Общий InventoryCache теперь использует defaultLogDirectory()/bios-inventory.json.
Обе GUI и mctl inventory сохраняют здесь последний снимок. Старый файл возле
EXE читается только при отсутствии нового, проверяется JSON schema/размер и
копируется с copy_options::none. Старая копия не удаляется, существующий новый
cache не заменяется старым. Обычный refresh атомарно обновляет лишь JSON.

В той же папке общая история постоянно дописывает rom_id и platform_id.
Один event — одна компактная JSON-строка с UTC/local временем, app_version,
session/sequence, origin/reason/history_context/action_id, before/requested/
observed/result/source/detail, последними наблюдениями платформы/ROM и полным
последним чтением региона770–77F со своим временем. Это два разных поля EC,
не UEFI UniWillVariable и не family-ID прошивки.

Старые schema1 строки rom_id не переписываются; новые schema2 добавляются.
ОС использует прежний OPEN_ALWAYS/FILE_END либо O_APPEND, file lock и flush.
Никакой ротации, truncation, rename/delete и зависимости от AllLogs/BatteryLog.
Истории не являются owner record и не разрешают операции с EC.

## Начальная точка и происхождение

Windows GUI вызывает recordStartupRomId до MainWindow/controller/restore,
включая активацию уже запущенного экземпляра; CLI — до ленивого создания EC
контроллеров для аппаратных команд. Прежние диагностические BIOS/VBIOS helpers,
help/version и --no-ec не получают нового неявного EC-доступа.
Стартовое чтение теперь7C3 плюс16байт770–77F через тот же readonly backend
и прежние exact-profile/driver/identity guards. Ни settings restore, ни writer
для снятия этой точки не создаются. Неподдерживаемые карты/Linux/disabled/read
error дают явную unavailable/unknown запись вместо подстановки FF/00/55AA.

origin=startup и reason=before_automatic_apply означают состояние, прочитанное
при запуске до наших автоприменений; это не заводские значения и не история
до всех прежних вмешательств. Уже работающий другой экземпляр может продолжать
свои действия: session/context/time сохраняют различие между запусками.

В реальных restore workers ставятся automatic и фактическая причина startup/
power_resume/system_sleep_clock. GUI SETs и CLI ставят user. Контекст истории
работает даже при выключенном обычном audit. API intent и транзакция могут
дать отдельные policy_requested с разными source; это не два write. Факт
записи обозначается write_requested, возврат — write_returned/unconfirmed,
фактическое чтение — write_readback/write_confirmed; ошибки и rollback отдельно.
Повторный выбор уже действующей политики также оставляет intent и снимки.

Наблюдаемая перемена сама не устанавливает внешнего writer: observation
и caller context не подменяют причинность. action_result=returned не объявляет
запрошенное применённым; требуется отдельный readback/последующий снимок.
Физическое удержание заряда и flash/NVM этим журналом не подтверждаются.

## Сохранение рабочих команд

В общем Change перед/после apply, commit и rollback добавлены только best-effort
снимки. После первой ошибки такой snapshot прекращает дальнейшие чтения,
сохраняя??, чтобы недоступный transport не дал17повторных IO timeout. Исключения
этих диагностических чтений/файлов не меняют исходный apply/rollback flow.
Маски, set-последовательность, ownership, identity admission и rollback прежние.
Core EcAccess и native stable reads наблюдают также7C3 без дополнительного poll.
Raw editor/API requests того же admitted RAM map также получают журнал.
INFO передаёт свежие чтения до display-retain, не выдавая старый cache за новое.

## Локальная подготовка и проверка

Приложение, EC/ACPI/GPU/driver/vendor helpers не запускались помощником.
Новые тесты приложения не добавлялись/не запускались. Выполнены компиляция обеих
ОС GUI+CLI0.1.199, обновление portable и file-only version/SHA/resource/source
checks в verification.json. Состав и payload BIOS/FPT/AFU/NVFlash не менялись;
геометрия/скролл не менялись. Проверена неизменность последовательности policy
set и единственного write callback относительно0.1.198.

Существующий Windows bios-inventory.json также скопирован в действующую папку
логов WUJIE без замены старого файла. platform_id создан пустым: реальные
стартовые значения помощником не выдумывались, появятся после пользовательского
запуска новой версии. Исходный префикс rom_id сохранён побайтово. Метаданные
операции: local-file-migration.json; сами личные JSON/истории не включены в Git.
В архиве пользовательской папки логов теперь будут все три файла. Чистые
релизные пакеты не должны включать личные логи, настройки или дампы.
