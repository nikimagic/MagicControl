# mctl для SSH

`mctl resources` выводит read-only каталог текущего компьютера в JSON;
`--watch --count N` выдаёт по одному JSON-объекту на опрос, `--no-ec`
отключает EC-источник. Каталог строится из одного существующего снимка
мониторинга, без дополнительного опроса. `Unknown` и `null` не означают ноль,
объявленные возможности профиля не разрешают команды, а время показания —
монотонное время конкретной машины, не UTC. Ресурсы могут содержать
идентификаторы устройства; полный JSON не копируется в журнал действия CLI.
`projection_check` сравнивает каталог с тем же снимком, без второго опроса:
число проверенных значений, расхождения и `verified`. Если значений нет,
`verified=false`; для независимых источников без такого сравнения поле `null`.
Это проверка преобразования снимка, а не физической исправности датчика.
`timestamp_origin` различает начало общего опроса (`collector_started`),
отметку приложения при чтении (`software_observation`), время сохранённого
значения в кэше отображения (`display_cache`) и неизвестное происхождение.
Эти отметки не являются временем измерения, сообщённым устройством; `retained`
описывает удержание значения приложением, а не гарантирует свежесть датчика.
Разные поставщики могут использовать разные монотонные часы; разницу между их
числовыми отметками нельзя считать точным возрастом или интервалом.

На Linux `mctl resources hwmon` отдельно читает стандартные `temp*_input`,
`fan*_input`, `in*_input`, `curr*_input` и `power*_input`. Имена и подписи
передаются от ядра; назначение канала не угадывается. Устройства с доступным
статусом suspended или suspending пропускаются. Температура переводится по
стандартному соглашению hwmon (миллиградусы); для отдельных чипов единицы
нужно сверять с документацией драйвера. PWM и настройки не изменяются. Доступность источника
описывает перечисление каталога hwmon, не исправность каждого датчика.
`--no-ec` с этим источником отклоняется: отдельные драйверы hwmon могут
обращаться к EC через ядро, и отсутствие такого доступа нельзя гарантировать.

На Windows `mctl resources drivers --no-ec` отдельно читает состояние службы
PawnIO и VERSIONINFO установленного SYS, а также версии PnP-пакетов для текущих
устройств ACPI INOU0000. Это метаданные: версия пакета, версия файла и версия
загруженного драйвера — разные поля; последняя остаётся `null`, поскольку не
запрашивается у ядра. Отсутствие устройства не доказывает отсутствие драйвера.
Команда не открывает EC, не запускает и не устанавливает драйверы и не выдаёт
допуск на запись. В ответе могут быть идентификаторы PnP-устройств; как и весь
каталог, полный JSON не копируется в журнал действия CLI.

HPBSD uses the same HpbsdFanController as GUI: `fans default`, `fans min`,
`fans max`, `fans manual CPU_PERCENT SYSTEM_PERCENT` (or `fans fixed`).
Inputs use the common5..100% range. Targets are nominal-RPM percentages, not PWM;
actual backend minima/thermal safeguards remain separate from editor input bounds.
Writes require `--yes` and the existing exclusive CLI/GUI control lease.
Manual/Min/Max stay foreground for their watchdog; `--count N` bounds a check.
Normal exit restores this controller's Default. `dump-ec` captures the full
4096-byte RAM window read-only, without fan probes or an active-mode change.

Начиная с0.1.199 `mctl inventory` также сохраняет bios-inventory.json в
logs_<actual board>, как GUI. JSON stdout остаётся прежним; ошибка сохранения
даёт stderr и exit4. Здесь же rom_id/platform_id: постоянная история startup,
automatic restore и пользовательских изменений, независимая от обычных логов.

Консольная версия возвращена по поручению владельца25.10.2026, начиная с0.1.192.
Исторический разбор ссылался на `0fb38b4^`/`0fb38b4`. После переписывания Git
эти commit IDs могут отсутствовать и не являются текущей навигацией.
Текущие сборочные настройки — в src/cli/CMakeLists.txt; старые прямые EC/полосочные
writers заменены адаптерами текущих типизированных API GUI.

## Пути и первый запуск

- Windows: `MagicControl/windows/mctl.exe` рядом с `MagicControl.exe`.
- Linux: `MagicControl/linux/mctl` рядом с `MagicControl`.

Примеры из portable-папки:

```powershell
.\mctl.exe --version
.\mctl.exe --help
.\mctl.exe models
.\mctl.exe board
.\mctl.exe status --watch --count 10
.\mctl.exe inventory
.\mctl.exe dump-ec --read-only
.\mctl.exe dump-bios
```

```sh
./mctl --version
./mctl board
./mctl status --watch --count 10
sudo ./mctl dump-ec --read-only
```

GUI можно запустить через SSH как процесс, но обычный SSH не показывает её окно.
Linux GUI требует графическое окружение/перенаправление X11. У mctl нет Gtk/Win32
инициализации окна; `--help`/`--version` возвращаются до контроллеров, логирования,
startup ROM и аппаратных запросов. Windows internal diagnostic helper dispatch
проверяет собственные служебные аргументы до обычной команды, как GUI.
`--dump-bios` поддержан также как совместимый alias `dump-bios`.

Windows PE — console/asInvoker. CLI не вызывает UAC или GUI permission dialogs:
нужны права вызывающего SSH-сеанса. Недостаток прав возвращает ошибку, не elevation.
На Linux нужны права к существующим sysfs/debugfs/provider API; unsupported
Windows-протоколы через чужие Linux EC-регистры не имитируются.

## Команды и связь с GUI

Результаты — UTF-8 JSON/JSONL на stdout, сообщения ошибок — stderr.
`--watch`, `--count N`, `--interval-ms N` управляют повторением чтений/foreground.
`--output DIR` разрешён для дампов, `--no-ec` сохраняет запрет EC.
Изменения требуют исторический флаг `--yes`; без него печатается только dry-run
запроса, без hardware. Dry-run не означает подтверждение поддержки или применения.
`--read-only` запрещает управляющие команды и FAN-пробы даже вместе с `--yes`.
Противоречие отклоняется общим parser до Runtime/SMBIOS/COM/log/backend init.
Чтение при этом может сохранять журналы и явный inventory cache: это запрет
управления оборудованием, не запрет файловых результатов диагностики.
Читающий CLI может работать рядом с GUI, удерживая отдельную свободную пару
логов. Если все три пары заняты, CLI сообщает отказ и не выполняет команду;
занятые журналы не перезаписываются и четвёртая пара не создаётся.
Неизвестные/недоступные показания остаются null/unsupported, а не0.

|Группа|Команды|Текущий общий путь и ограничения|
|---|---|---|
|Каталог/идентичность|models, board|deviceProfiles/readBoardDescriptor, фактическая SMBIOS; нет подстановки модели из меню|
|FANs/телеметрия|status, diag, fans status|MonitoringReader, read-only diagnostic hardware, GPU selection по нагрузке и источники GUI|
|INFO|inventory|defaultHardwareProvider().collect/toJson, общий формат INFO|
|EC-дамп|dump-ec|createDiagnosticDump, по умолчанию ReadOnly и минимум60сек; probes только с двумя явными флагами --with-fan-probes --yes|
|BIOS|dump-bios|createBiosRomDump: общий automatic reader admission AFU/FPT16/FPT19/допущенный Insyde READ; точные платформенные ограничения и отказы сохраняются, включая запрет неподтверждённого Insyde на Lenovo|
|Вентиляторы|fans default/max/manual/online FILE, fans fixed CPU GPU|FanWriteRequest/executeFanResource; exact controllers, общий readback и caller lease. Manual EC и Online различаются; fixed5..100, Min из native state. Прежний foreground watchdog сохранён|
|Мощность|power status/economy/office/gaming/balanced/turbo/auto/custom A B C/slot N|Общие текущие setters; порядок полей как на модели GUI. Honor — read/custom SMU. Неизвестный preset остаётся отказом|
|Батарея|battery status/limit/restart/thresholds/policy/protection/view/aging/native-turbo|Общий BatteryWriteRequest/executeBatteryResource; реальные model adapters сохраняют свои guards/rollback. Native pair/upper/restart общие для Windows/Linux; policy/protection/aging требуют подключённого backend. Physical enforcement не объявляется по readback|
|Клавиатура|keyboard status/level/effect/color/pulse/default/timeout/cycle|Общий KeyboardBacklight API/Gm6/Huawei/HID. Color0–255 переводится родным codec. Pulse — native index1–4; effect mono/pulse/multi. Timeout — typed Honor|
|Зоны HID|lighting zones/set ZONE LEVEL EFFECT SPEED DIRECTION R G B|Только фактически подтверждённые HYDROC зоны IteLightingController; независимые параметры сохраняются его API|
|KEYs|keys NAME [on/off]|Тот же QuickSwitch, typed Honor Fn, системный микрофон/устройства Windows. Не переносит Uniwill на Honor|
|GPU|gpu status/power VENDOR ID VALUE/reset VENDOR ID|Общий с GUI executeGpuOperation и DeviceOperation reservation, точный provider ID. NVIDIA VALUE в Вт, Radeon — native единица карты (обычно%); прежний backend перепроверяет пределы/identity/readback|
|LPP|lpp status/discover|Существующий inert/read-only интерфейс; команды помпы/Auto не добавлены в CLI, поскольку UI для них тоже ещё нет|

Manual/online читает файл именно формата GUI `fan-curves.csv`. Логический
Up/Down/PWM codec вынесен в одну `customCurvesFromFanProfiles`, которую используют
GUI и CLI. Это не перенос аппаратной таблицы одной модели на другую.

Примеры явных запросов (выполнять самостоятельно только на поддерживаемой модели):

```text
mctl fans default --yes
mctl fans online settings/fan-curves.csv --yes
mctl power custom 25 35 45 --yes
mctl power slot 1 --yes
mctl battery limit 95 --yes
mctl battery thresholds 70 80 --yes
mctl keyboard level 2 --yes
mctl keyboard effect pulse --yes
mctl keyboard pulse 2 --yes
mctl keys microphone off --yes
```

CLI не воспроизводит UI-диалоги выбора цвета/файла: значения/пути передаются явно.
`keyboard level` — явное изменение уровня; effect/color — отдельные команды.
Поддержка модели не означает поддержку каждого setter. На Linux сохранён
существующий typed Huawei/sysfs путь для батареи/Fn/клавиатуры и системный
микрофон; прочие отсутствующие writers не включены ради синтаксического сходства.
Windows Wi-Fi/Bluetooth чтение доступно; запись требует GUI radio consent,
который новый SSH-процесс не запрашивает, поэтому такой запрос получит отказ.

## Владение, процесс и журнал

Windows-команды изменения берут общий `Global\\MagicControl.SingleInstance.v2`.
Linux GUI и изменяющие команды CLI держат одну OS-аренду `flock` на
`/tmp/magiccontrol.application-control.v1.lock`; файл остаётся на месте,
блокировка освобождается при завершении процесса. Первичный GTK-процесс
получает её до создания аппаратных контроллеров. Обе стороны должны быть
версии0.1.403 или новее: старая GUI в новом протоколе аренды не участвует.
При работающей GUI/другом CLI writer возвращается ошибка: нет handoff,
автоматического завершения GUI или параллельного владельца. Read-only запросы
могут сосуществовать с GUI. Runtime cache и driver/hardware guards прежние.

Нет общего startup restore. Для CLI Gm6Controller создаётся с
startupQuickRestore=false до старта worker; GUI сохраняет прежнее true.
Это не позволяет одному SSH-запросу автоматически воспроизвести чужие KEYs.
`recordStartupRomId` сохраняет текущий append-only контракт для обычных hardware
команд; для BIOS не выполняется EC startup read, board остаётся OS-only.

Foreground online/цветовой цикл/фиксированный Honor работают пока жив процесс.
Ctrl+C/SIGTERM кооперативно завершает текущую операцию. Завершение контроллера
использует его прежний core lifecycle; CLI не добавляет сброс Default/таблиц на
выходе. У Honor собственный manual request освобождается родным watchdog/controller.
Потеря SSH/принудительное уничтожение процесса не обещается эквивалентом чистого
Ctrl+C; для длительного контроля нужна сохранённая SSH-сессия/tmux/служба ОС.

Подтверждённые настройки сохраняются существующими model APIs. CPU, подсветка и
батарея используют общие resource entries и свои completion owners. Для батареи
нет отдельного CLI или GUI settings writer: Wujie сохраняет фактическую пару,
native controllers — свою подтверждённую конфигурацию, Honor Windows/Linux —
общий lower/capacity store. При отказе чтения или сохранения CLI сообщает ошибку,
даже если сама аппаратная команда уже выполнена.
Restore и временная индикация не сохраняют новый пользовательский выбор.
`power slot 1/2/3` переводится в внутренние индексы0/1/2 только на входе CLI:
тот же Custom пресет мощности и FANs, что у кнопок1/2/3 GUI.
CLI не выполняет startup restore всех GUI preferences. Результаты
применения, конфигурация и физический эффект остаются разными понятиями.
Логи используют предпочтения AllLogs/BatteryLog и общий paired logger, version
в начале файлов, audit command scopes/ответы и общую историю rom_id.

Коды:0 — выполнено/выведен ответ,1 — usage,3 — backend unavailable,
4 — отказ/ошибка выполнения,6 — unsupported. Поля available/connected/status
в snapshots нужно проверять отдельно: успешный getter может сообщить unavailable.

## Проверка и сборка

Build-All собирает GUI и mctl обеих ОС одним номером, stager копирует оба,
версия/оба SHA проверяются до статуса verified. Windows CLI также имеет PE
ProductVersion и console subsystem3. Новые reader bytes встроены и в mctl,
поэтому BIOS dump не зависит от запущенного графического приложения/скачивания.
GitHub-архивы этим поручением не готовились. Сборочные/file-only receipts:
`research/cli-restore-20261025/verification.json`. Аппаратные команды помощником
не запускались; реальные SSH/контрольные операции на ноутбуках не проверены.

## VBIOS и EEPROM NVIDIA,0.1.196

`mctl dump-vbios [0000:01:00.0] [--output DIR]` доступен в Windows/Linux.
Если NVIDIA несколько, PCI-адрес обязателен. Используется тот же collector,
что в INFO: VBIOS и полный EEPROM через --entire, по2чтения, проверка
распознанной ёмкости/размеров/SHA256/PCI ROM/raw compare, полный raw log в
`vbios-dumps`. В JSON отдельно vbios_saved/eeprom_saved/capacity_verified/
eeprom_bytes. Exit0 — оба результата; exit4 — partial/отказ/ошибка с файлами.
Нужен elevated Windows или root Linux сеанс; диалоги повышения прав не вызываются.
EC/ROM-ID startup не читаются, `--yes` для этого явного read/save не требуется.
`--dump-vbios` — alias. [Контракт и ограничения](VBIOS-DUMP.md).
