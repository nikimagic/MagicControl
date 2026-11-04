# KEYs: сохранение и восстановление,27.10.2026

## Текущее решение: единый KEYs для любого компьютера

### Явные команды и GUI,0.1.400

Один KeysOperations в KeyResourceControl.h / core/src/KeysLifecycle.cpp
принимает submit/refresh/observe от Win32 и GTK. Он владеет явной очередью,
worker, DeviceOperation reservation, классификацией результата, read overlays
и данными завершения OSD. Страницы только передают события и показывают готовый
KeyOperationResult; потоков/pending arrays/очередей/KeysResult в них больше нет.
OS factories сохраняют typed context живым до join. Подготовка radio, OSD и
локальные preferences вызываются на UI thread; аппаратные callbacks — на worker.
Отложенный за чтением ключ перепроверяет доступность и завершает OSD даже при
новой недоступности. Windows GUI/CLI получают один KeyBackendResult вместо
старого DTO/callback-выходов. Только fresh observation подтверждает запись.

Прежний KeysLifecycle принадлежит координатору, но его restore/observe worker,
retry/epoch/suspend и запрет Bluetooth replay сохранены без изменений. Это
разные политики над теми же typed setters, не две реализации аппаратной команды.
Геометрия и единый скролл KEYs не изменены; схема/путь настроек также прежние.

### Микрофон и Wujie mute LED,30.10.2026

Единственный resource handler MicrophoneKeysWindows применяет Windows mute,
получает fresh readback и только затем зеркалит фактический mute в EC7A6.2.
Вкл.=capture enabled/LED flag0,Выкл.=mute/LED flag1. Флаг не является доказанным
codec-disable и не гарантирует физическое наличие диода. Профиль+живые82/72/26
проверяются при EC-доступе; используется только маска04. Другие биты общего
байта и FAN/APU/battery/APP поля не меняются. Другие модели сохраняют свои
системные microphone handlers без Wujie EC-карты. Нет отдельной LED настройки.

GUI, CLI и restore используют общий applyKeyHardware. Getter показывает
Windows enabled и отдельное состояние EC indicator flag. Недоступность/ошибка
LED не отменяет подтверждённый Windows mute, не включает микрофон обратно и
не мешает сохранению основного enabled в keys-settings.ini.

Audio endpoint callbacks остаются наблюдателями без EC backend. Событие mute
через KEYs.observeState только ставит coalesced notification в существующий
KeysLifecycle worker. Режим Observe общей операции читает текущий default
capture и обновляет индикацию, никогда не вызывает Windows mute setter и не
откатывает новое состояние к старому bool из события. Тот же worker сохраняет
фактический внешний выбор после fresh readback. Restore сохраняет приоритет,
сон прерывает работу по прежней эпохе, новый poller/thread не добавлен.

### Copilot: один аппаратный KEYs-путь,30.10.2026

Владелец отменил программный перехватчик и отдельный пятисекундный sync.
CopilotKeyBlocker.cpp/.h удалены из рабочего проекта. MainWindow не создаёт
блокировщик, не читает/не пишет его EC-флаг, не восстанавливает Copilot отдельно
от KeysLifecycle. F23/Win+C не перехватываются; OSD/F24 observer не менялся.

Copilot теперь обычный KeyAction с ресурсом copilot-lock в общей таблице
applyKeyHardware. Вход общий для GUI, CLI keys copilot и startup/resume через
KeysLifecycle; очередь/DeviceOperation admission те же, что у других ключей.
После native setter следует fresh readback; только подтверждённый результат
сохраняется в прежнее CopilotBlocked в keys-settings.ini. Импорт старых файлов
сохранён, schema/version не менялись. ApplicationPreferences больше не содержит
copilotBlocked и не создаёт второй источник состояния. KEYs показывает getter,
не файл предпочтений. При неизвестном native transport кнопка недоступна.

Для подтверждённых Uniwill profiles сохраняются прежние EC/typed-EC команды.
Для другого hardware допустим исходно проверенный OEM keyboard WMI ABI, а не
подстановка728 на любую плату. Один device context выбирает один transport;
нет параллельного software blocker, опроса или второго hardware writer.
Wujie getter учитывает728.2 и APP741.0. Inactive effective state может скрывать
latched728.2, поэтому native adapter просит однократное idempotent reconcile
в общем lifecycle: saved Off снимает бит даже при APP0. Остальные setter-ы
не получают новых повторов. Bluetooth policy и FAN/APU writers не менялись.

Linux по-прежнему не имеет подтверждённого Copilot adapter и не имитирует
блокировку сохранённой галкой. Разделы ниже — исторические проверки прежних
сборок; их hook/watchdog-пути заменены текущим контрактом.

### Нативная блокировка Copilot из OEM,0.1.249

**Подтверждено пользовательским прогоном28.10.2026 на Wujie:** короткие
нажатия и удержание5сек больше не открывают Copilot. Журнал0.1.249 показывает
EC native readback1 при старте05:24:20МСК,0после Off05:25:45 и1после On05:26:01.
Ошибок command_result=failed в этом снимке нет. Сохранено CopilotBlocked=1.
Это подтверждение именно данного Wujie и пользовательских нажатий, не всех
моделей/перезагрузки/гибернации. Данные: runtime-validation-0.1.249.json в research.

Повторный пользовательский прогон0.1.248 НЕ подтвердил надёжную программную
блокировку: Copilot снова открылся, хотя copilot suppressed F23_events продолжал
увеличиваться и в KEYs оставалось CopilotBlocked=1. Точную причину обхода hook
этот журнал не устанавливает. По требованию владельца разобраны OEM реализации.

Подтверждено по исходным артефактам:

- OEM Wujie GCUService5.57.1.2, восстановленный по точному установленному/installer
  SHA: UserSetCopilotKey сохраняет0/1 и вызывает SetLCWMI("SetCopilotKey",status).
  Тот запускает UEFI_Firmware.exe --SetCopilotKey0|1. EXE не запускался.
- Native helper SHA256 B90A4416DC19EF9A529E0E69B8955E0C46F0ACC81FFA831409C7EC3541E3B97C,
  GetCopilotKey RVA20F0/SetCopilotKey RVA2310: ROOT\\WMI, OemWMIMethod,
  OemWMIfun, instance ACPI\\PNP0C14\\HWMI_0,64-byte u8Input/u8Output.
  Getter:03 05+zeros; setter:03 06 bool+zeros; status byte0 должен быть0,
  getter byte1 —0или1. На текущем Wujie read-only class lookup не нашёл этот
  WMI класс, поэтому ни getter, ни setter через него при проверке не выполнялись.
- HYDROC5.23.51.56 recovered MySettingManager.SetCopilotKey и L16 5.60.60.17
  recovered GCUService: RMW EC1832decimal=0x728, mask0x04. Lock: OR4;
  Unlock: AND0xFB. Соседние биты не меняются.
- Из Wujie BIOS сохранён EC-core update-payload candidate SHA256
  5d4ec03a4ec9b36938d907fd6fcb51109b445b4a43c5498f018d022d4e6ac7a8:
  C47F читает728.2 в ветке внутреннего keycode F8; C76D требует741.0.
  CB9D очищает728.2 при соответствующей firmware инициализации. Сам факт
  извлечения payload не является независимым чтением исполняемого EC flash.

Один platform factory выбирает распознанный OEM WMI ABI по реальным методу/
параметрам/64-byte ответу или подтверждённую EC ABI через существующий транспорт.
Для raw EC capability metadata допущены Wujie/L16/HYDROC по указанным данным;
адреса не угадываются для других firmware. На неизвестной модели WMI путь
возможен при наличии того же подтверждённого интерфейса; при отсутствии native
остаётся явно программный fallback, без обещания аппаратного подтверждения.
Механика KEYs,11полей и один keys-settings.ini не зависят от выбранного шасси.

EC writer меняет только728.2 и при необходимости общий AP-present741.0;
прочие биты сохраняет. Presence сам не выбирает режим FAN. Off не освобождает
общий APP-бит других функций. После firmware reset native sync пишет только
при fresh mismatch и свободном DeviceOperation, не вмешиваясь в FAN/APU apply.
OEM-owner guard сохраняется, службы не останавливаются. Setter/getter readback
журналируется отдельно от физической проверки, которую ещё надо повторить.
Нативный выбранный флаг при выходе не сбрасывается; UI больше не обещает для
всех backend действие исключительно во время открытой программы.

### Copilot/OSD и физический перехват,0.1.248

Лог0.1.247 от27.10.2026 21:51:29/21:51:33/21:51:40МСК показал,
что Copilot переключатель сохранялся в старый app-settings.ini. Пользователь
сообщил, что физическая клавиша при включённой блокировке не блокируется.
Это отдельное наблюдение: configuration_saved не было доказательством перехвата.

Все11полей KEYs теперь принадлежат keys-settings.ini Version3, включая
OsdDisabled/CopilotBlocked. Старые Version1/2 читаются, явные значения сохраняются.
При старте старые OSD/Copilot переносятся без перезаписи существующего KEYs-выбора;
исходный app INI копируется в settings/legacy-backup/app-settings-before-keys-v3.ini,
затем только эти2строки удаляются из активного app INI. Новые шаблоны app INI
их не содержат. Язык/шрифт/логи остаются app-настройками, не KEYs. Ни app save,
ни переключение языка не переписывают KEYs-флаги. Откат неудачного KEYs save
возвращает runtime состояние и программный hook к предыдущему значению.

Прежний hook устанавливался из UI-потока и проверял только VK_F23. Новый
CopilotKeyBlocker имеет собственный message loop/поток с подтверждением команд,
обрабатывает F23 и обычный Win+C, переустанавливается по явному On и resume.
Ctrl+C, Win+Ctrl+C, Win+Shift+C и F24 не блокируются. Отпускание ранее
пропущенного C/F23 не подавляется, чтобы не оставить клавишу логически зажатой.
Callback только фильтрует нужные события/увеличивает два atomic счётчика;
логирование выполняется из UI timer вне callback. Данные обычного ввода не пишутся.
Появились copilot hook_state и suppressed F23_events/WinC_events: регистрация
отделена от реально обработанных клавиш. Новая физическая проверка ещё требуется.

Основания: [Microsoft LowLevelKeyboardProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)
описывает поток установки, message loop и молчаливое удаление при timeout,
рекомендуя отдельный поток. [Microsoft Copilot shortcuts](https://learn.microsoft.com/en-us/copilot/windows)
подтверждает Win+C. Отсюда следует риск прежнего UI-hook, но его фактическое
удаление в конкретном пользовательском сеансе не доказано. Временный90с счётчик
F23/Win+C дал0без подтверждённого нажатия в его окне; сигнатуру физической
клавиши по этому результату не угадывали. Перехват обычных клавиш не записывался.

### Отдельная схема KEYs и проверка portable0.1.246

Свежие логи27.10.2026: старты21:21:28 и21:22:07МСК, версия0.1.246.
Bluetooth на обоих стартах preserved; после явного Off сохранился Bluetooth=0.
Из22финальных пользовательских действий20подтверждены,2ошибки — Microphone Off
в21:22:20 и21:24:40МСК. Фактическое выключение наблюдалось как0, но запись INI
была отклонена. Остальные сохранённые переключения совпали с keys/app INI.
Fn отдельно не переключали: его1подтверждено чтением при старте. Записей
power_resume/system_sleep_clock в этих двух сессиях нет; проверена повторная
загрузка приложения, а не сон/гибернация/перезагрузка компьютера.
Выборки и хэши снимков: research/keys-audit-20261027/portable-0.1.246-evidence.json.

Причина — ошибочное использование DevicePreferences для файла KEYs: добавленный
Microphone попадал в числовую ветку другой схемы. В0.1.247 это переиспользование
УДАЛЕНО: KeysPreferences содержит только optional<bool>, собственные имена полей,
чтение0/1 и запись0/1. Нет DeviceSetting::Microphone и вызовов loadDevicePreferences,
rememberDevicePreferences или validDeviceValue для keys-settings.ini. Из legacy
INI при первом импорте выбираются только известные строки переключателей;
поля питания/батареи/подсветки даже не разбираются. Используется общий низкоуровневый
атомарный file writer, который не проверяет предметные значения.
Прежние файлы keys-settings.ini версии2 совместимы и не заменяются обновлением.
Ранее не записанный выбор микрофона нужно подтвердить новой кнопкой после обновления;
живое состояние ОС не используется для выдумывания отсутствующего сохранённого выбора.

Пользователь уже повторно проверил0.1.247: в21:49:27/21:49:31МСК Microphone On/Off
получили readback_confirmed, в отдельном keys-settings.ini присутствует Microphone=0.
Fn Off/On, Bluetooth On/Off, Camera On/Off и Touchpad Off/On тоже подтверждены.
Startup21:48:27МСК реально восстановил сохранённый Touchpad=0 с write=1;
Bluetooth preserved, остальные совпадавшие поля были no-op. Это аппаратное
наблюдение пользовательского прогона, не запуск тестов/команд помощником.
Снимок: research/keys-audit-20261027/portable-0.1.247-evidence.json.

После ревизии владелец потребовал один режим KEYs/INFO для всех, включая
неизвестные и неподключённые модели. Прежний отчёт0.1.245 НИЖЕ — baseline,
а не описание новой реализации. Новый общий модуль — KeysLifecycle.h/.cpp:

- Все Windows/Linux страницы используют одну схему11KeyAction и один engine
  startup/resume. Системные функции не зависят от EC/SMU/FAN/AC admission.
- Один settings/keys-settings.ini: явные Win/Fn/USB/AC/Wi-Fi/Bluetooth/camera/
  touchpad/microphone choices. Старые выбранные поля импортируются из INI
  реального устройства один раз, без подстановки live-reset состояния и без
  перезаписи уже существующего общего файла. Нулевые значения сохраняются.
- OSD/Copilot с0.1.248 используют keys-settings.ini; их runtime-объекты
  применяются из UI при старте/resume и не зависят от EC/FAN/APU.
- Bluetooth никогда не переигрывается при lifecycle. Явные кнопки работают.
- Fn Honor и микрофон теперь имеют app persistence/replay после подтверждённого
  нажатия; они не зависят от battery/power controllers.
- Из Wujie и GM удалены отдельные KEYs replay loops: кнопки и lifecycle вызывают
  прежние checked setters. FAN/CPU/lighting/battery механизмы не переписаны.
- Windows подписывает KEYs на startup и все3resume broadcasts даже без EC или
  известного профиля. Linux подписывает тот же engine на logind PrepareForSleep.
  Новый процесс после reboot использует startup. На suspend очередь отменяется.
- Для каждого поля есть fresh read и статус confirmed/skipped/unsupported/
  unconfirmed/failed_after_command/incomplete. Чтение/operation gate могут ждать
  до30проходов по1сек; отправленная неоднозначная команда не повторяется вслепую.
- В Unknown и каталоге Windows доступны OSD/Copilot/Wi-Fi/Bluetooth/camera/
  touchpad/microphone при наличии реального устройства ОС. Каталог не создаёт
  аппаратный допуск: Fn/USB/AC/EC-WinLock чужой модели остаются запрещёнными.
  Фоновый worker скрытой реальной страницы при входе в каталог приостанавливается.
- Linux общий engine обслуживает уже доступные OSD/microphone/Honor-Fn adapters.
  BlueZ/NetworkManager/V4L2/libinput ещё не реализованы; открытая вкладка не означает
  выдуманную поддержку этих backend. Для любой платы есть единая страница и одна
  политика, недоступность относится только к конкретной функции.
- INFO и до изменения имела biosDeviceActionsAllowed=true для любого компьютера.
  Это закреплено комментарием и AGENTS.md; выбор каталога не подменяет машину,
  vendor/reader checks остаются в BIOS/VBIOS/EC collectors.

Аппаратный сон, гибернация, reboot и переключение устройств помощником не
запускались. Сборка и статическая трассировка не являются их физическим тестом.

## Baseline0.1.245: что было найдено до общего модуля

## Исправления ревизии

Bluetooth — системное состояние Windows. Запуск MagicControl, обновление,
возобновление и прочие вызовы lifecycle restore не меняют его. Старое
Bluetooth=1/0 в INI не является командой для восстановления. Кнопки KEYs
по-прежнему вызывают checked setter с проверкой результата и сохраняют
успешный выбор как последний выбор приложения. Явная CLI-команда также
доступна, но сама не сохраняет DevicePreferences. Общая политика
restoreDevicePreferenceOnLifecycle применяется в обоих Windows restore workers.
Журнал явно пишет RESTORE skipped: Bluetooth; Windows lifecycle state preserved.
Наличие старого Bluetooth=1 в файле больше не включает адаптер.

KEYs save и GM6 restore используют один keysPreferencesPath: Wujie/HPBSD —
device-settings.ini; GM6/GM5/L16/HYDROC — свой model-device-settings.ini;
Honor — hlyl-wxx9-device-settings.ini. Все файлы находятся в settings.
Раньше GM6 сохранял Wi-Fi/камеру/тачпад в модельный INI, а читал общий INI.
Старые общие значения не импортируются поверх модельных: отличить старое
намерение от пассивного снимка без истории нельзя.

Wujie больше не пишет RESTORE confirmed для устройства с неизвестным состоянием:
это ошибка чтения, без записи. Равное фактическое состояние допускает confirmed
без повторной команды. Поэтому confirmed само по себе не доказывает запись.

Linux KEYs перед изменением OSD перечитывает app-settings.ini: прежняя копия
страницы могла затереть язык, выбранный после её создания. При ошибке чтения
запись отменяется. Windows использует общие ApplicationPreferences главного окна.

## Матрица Windows

| Строка | Сохранение | Восстановление приложения |
| --- | --- | --- |
| Windows Lock | Wujie/model INI после readback | Wujie + admitted Uniwill quick restore |
| Отключить OSD | app-settings.ini | Читается общими настройками приложения |
| Fn Lock | Wujie/model INI; Honor setter HWMI без файлового сохранения | Wujie/Uniwill; Honor firmware persistence отдельно не доказана |
| USB charging | Wujie/model INI после readback | Wujie + admitted Uniwill quick restore |
| AC recovery | Wujie/model INI после readback | Wujie + admitted Uniwill quick restore; результат запуска требует отдельной проверки |
| Copilot Lock | app-settings.ini | Windows hook во время работы программы |
| Wi-Fi | keysPreferencesPath после readback | Wujie/GM6 full restore |
| Bluetooth | keysPreferencesPath после readback | Не переигрывается; актуальное состояние Windows |
| Камера | keysPreferencesPath после readback | Wujie/GM6 full restore |
| Тачпад | keysPreferencesPath после readback | Wujie/GM6 full restore |
| Микрофон | Системное mute, без DeviceSetting | Нет app replay; состояние ОС |

Системные Wi-Fi/камера/тачпад GM5/L16/HYDROC/Honor/HPBSD сохраняются кнопками,
но полного системного replay у этих семейств сейчас нет. Не добавляли его
попутно: это расширение поведения, требующее отдельного согласования.
Пассивное наблюдение/нажатия вне KEYs не перезаписывают подтверждённый app выбор.
Это особенно существенно для Wi-Fi/камеры/тачпада, где full restore ещё действует.

## Linux

Работают OSD, Honor Fn через sysfs и системный микрофон PulseAudio/PipeWire.
BlueZ/NetworkManager/V4L2/libinput не подключены к KEYs; соответствующие строки
недоступны, сохранение или аппаратное применение для них не имитируется.
Copilot/Uniwill quick-switch route недоступны в текущем Linux адаптере.

## Проверенные данные и ограничения

В локальных Wujie логах0.1.243/0.1.244 от27.10.2026 сохранён Bluetooth=1;
при стартах14:52:30Z,15:11:52Z,15:24:36Z worker выбирал его и писал confirmed.
Это соответствует жалобе, но прежний confirmed мог означать no-op: сам по себе
он не доказывает SetStateAsync. В коде найден реальный путь, который при
Bluetooth Off и сохранённом1 вызывал SetStateAsync(On); теперь он пропускается.

DevicePreferences использует optional<int>: значение0 НЕ считается отсутствием.
Merge сохраняет присутствующие нули и остальные поля, запись атомарная, под mutex.
Настройки пользователя, радио и EC при ревизии вручную не переключались.
Аппаратные тесты и тестовые targets не запускались согласно AGENTS.md.

## Полная ревизия всех моделей и переходов

Охват:17 записей DeviceRegistry + unknown,11 строк KEYs,Windows/Linux.
Это статическая проверка всех ветвей сохранения/dispatch/lifecycle, а не
физическое подтверждение сна/гибернации на18 разных компьютерах.

### Доставка событий

Windows MainWindow принимает PBT_APMSUSPEND и три resume события:
PBT_APMRESUMEAUTOMATIC/PBT_APMRESUMESUSPEND/PBT_APMRESUMECRITICAL.
У GM6-семейства resume вызывает restoreConfiguration, затем quick-switch worker;
у Wujie — resumePower и ConfigurationRestore. Тот же путь обслуживает выход
из гибернации при получении resume события Windows. Раздельного S4-protocol нет.
Повторные уведомления объединяются; пробуждение не является подтверждённым
успехом до нового readback. Восстановление после перезагрузки выполняется
только при фактическом запуске MagicControl: нет отдельной KEYs службы,
восстанавливающей эти INI, пока приложение не запущено.

Wujie дополнительно имеет FanSleepTracker и notifier system_sleep_clock:
разрыв часов сна вызывает повторную проверку даже при пропущенном broadcast.
Его notifier подключён только к Wujie FanController. На GM6 и остальных
семействах нельзя приписывать наличие этого запасного механизма.

Linux KEYs не имеет подписки PrepareForSleep/login1 и не запускает replay
после сна/S4. OSD остаётся состоянием живого процесса; системный микрофон и
Honor Fn перечитываются, но прежний выбор приложения не воспроизводится.

### Матрица моделей Windows: старт / сон / S4

«Да» — существует путь проверки и восстановления при каждом из трёх событий,
с admission/readback/ownership guards; это не гарантия физического результата.
Bluetooth везде оставляет текущий выбор Windows. OSD/Copilot во всех строках
читаются при старте из app-settings.ini и остаются в памяти после сна/S4.

| Профиль DeviceRegistry | Windows/Fn/USB/AC: старт/сон/S4 | Wi-Fi/камера/тачпад: старт/сон/S4 |
| --- | --- | --- |
| wu-jie-15-xpro | Да/да/да | Да/да/да |
| mechrevo-gm6ir0c | Да/да/да | Условно/условно/условно: зависит от полного native restore |
| mechrevo-gm5hg0a | Да/да/да | Нет/нет/нет |
| machenike-l16 | Да/да/да | Нет/нет/нет |
| eluktronics-hydroc16g2 | Да/да/да | Нет/нет/нет |
| huawei-hlyl-wxx9 | Fn: HWMI,без app replay; остальные EC quick недоступны | Нет/нет/нет |
| minisforum-hpbsd | EC quick недоступны | Нет/нет/нет |
| mechrevo-gm6px7x | EC quick недоступны | Нет/нет/нет |
| mechrevo-kuangshi-jiguang-series-x6xx55xw-b2 | EC quick недоступны | Нет/нет/нет |
| mechrevo-canglong-series-x6xr55xy | EC quick недоступны | Нет/нет/нет |
| mechrevo-gm6bg0x | EC quick недоступны | Нет/нет/нет |
| mechrevo-kuangshi-jiguang-series-x6xx57tw | EC quick недоступны | Нет/нет/нет |
| mechrevo-jiaolong-series-x6dr55xx-b2 | EC quick недоступны | Нет/нет/нет |
| mechrevo-gx6hg0u-f | EC quick недоступны | Нет/нет/нет |
| mechrevo-canglong-series-x8xr55xp-b2 | EC quick недоступны | Нет/нет/нет |
| mechrevo-yaoshi-series-x6ar55xy | EC quick недоступны | Нет/нет/нет |
| mechrevo-gm6ix9b | EC quick недоступны | Нет/нет/нет |
| unknown | EC quick недоступны | Нет/нет/нет |

У inventory-only профилей перечисление модели не разрешает EC-команды.
Windows системные строки могут работать через общий адаптер независимо от
наличия подтверждённого EC-протокола. Предпросмотр другого профиля запрещает
все управляющие действия; app выбор не сохраняется от такого просмотра.

### Оставшиеся подтверждённые пробелы

1. **GM6: системные KEYs связаны с native FAN/APU restore.** До цикла
   Wi-Fi/камеры/тачпада выполняются native admission,ожидание nativeArmed,
   применение мощности/вентиляторов. Для сохранённого Custom/Economy без AC
   функция выходит configurationWaitingForAc до KEYs. Ошибка этих применений
   также выходит до KEYs. Исправление пути INI в0.1.245 не устраняет эту связь.
   Нужен независимый системный restore, без изменения FAN/APU алгоритмов.
2. **GM5/L16/HYDROC/Honor/HPBSD и inventory-only:** системный выбор
   Wi-Fi/камеры/тачпада записывается,но не читается для replay. Восстановление
   ОС/прошивкой возможно,но это не восстановление MagicControl.
3. **Honor Fn:** Windows setter использует HWMI get0604/set0704,а Linux
   sysfs fn_lock_state; файлового сохранения и replay нет. Прошивочная
   сохранность после S3/S4/reboot в этой ревизии физически не измерялась.
4. **Микрофон всех моделей:** нет DeviceSetting. В Windows меняется mute
   текущего default capture endpoint,в Linux — текущего default source.
   Смена/пересоздание устройства не сопровождается app replay прежнего mute.
   Setters проверяют идентичность endpoint до/после и не сохраняют догадку.
5. **Аппаратные/внешние переключения:** observeState и опрос обновляют UI,
   но не INI. Поэтому физически изменённый WinLock/тачпад/камера или Wi-Fi,
   изменённый через Windows,может вернуться к старому app выбору там,где
   replay уже подключён. Пассивное чтение неизвестного/сброшенного состояния
   нельзя просто записывать как новое намерение пользователя.
6. **Copilot:** после старта сохраняется установленный hook. Его здоровье
   отдельно после resume не проверяется; по наличию объекта нельзя доказать
   физическую блокировку. Проверенного отказа hook в имеющихся данных нет.
7. **Linux:** отсутствует lifecycle replay KEYs вообще; недоступные аппаратные
   строки не следует объявлять работающими из-за существования Windows-кода.

Эти пункты перечислены как результат полной ревизии,не исправлены скрытым
расширением поведения в0.1.245. Исправлены только четыре дефекта выше.

### Что не потеряно в сохранении

Нули0/false не пропадают: optional хранит присутствие независимо от значения.
GUI сохраняет после checked setter/readback; ошибка сохранения явно отличается
от ошибки применения. OSD/Copilot — общий INI,прочие сохранённые переключатели
— DevicePreferences выбранной реальной модели. Файлы мигрируют через общий
settings механизм; выбор другого профиля для просмотра не меняет файлы.
EC quick setters используют маски768.0/74E.4/767.4/726.3 и не перезаписывают
весь общий байт. Windows Lock посылает toggle один раз,проверяет768.0 и не
повторяет неоднозначную команду вслепую. USB/AC readback — подтверждение бита,
а не фактической работы USB вS4 или включения от адаптера при выключенном ПК.

### Точки кода для повторной проверки

- src/gui/common/KeysPolicy.h:11 действий,OS/model/preview admission.
- src/gui/windows/KeysPage.cpp:start/apply/refresh/observeState/complete.
- src/gui/windows/MainWindow.cpp:create,WM_POWERBROADCAST,restoreConfiguration.
- src/devices/mechrevo/x5sp4nag/windows/ConfigurationRestoreWindows.cpp:capture/run.
- src/devices/mechrevo/x5sp4nag/windows/FanControlWindows.cpp:FanSleepTracker/notifier.
- src/devices/mechrevo/gm6ir0c/ControlWindows.cpp:restoreQuickSwitchTick,
  restoreConfigurationTick,restoreKeyboard,persistentModel.
- src/protocols/uniwill/QuickSwitch.cpp:маски,readback,ограничение toggle.
- src/platform/windows/WindowsDevices.cpp:радио,камера,тачпад.
- src/platform/windows/CopilotKeyBlocker.cpp:setEnabled/keyboard.
- src/platform/windows/MicrophoneControlWindows.cpp:default endpoint.
- src/gui/linux/KeysPage.cpp и OsdService.cpp:preferences/read-only polling.
- src/platform/linux/huawei-wmi/HuaweiControlLinux.cpp:Fn sysfs setter.
- src/platform/linux/MicrophoneControlLinux.cpp:PulseAudio/PipeWire.
- src/core/src/PortableSettings.cpp:paths,merge,atomic write и migration.

## Полная ревизия всех девайсов и событий питания

Область: все17профилей DeviceRegistry плюс unknown и предпросмотр,11строк KEYs,
Windows/Linux. Проверено по текущему коду, а не по общему названию ControlCenter.
Версия исправлений выше —0.1.245. Матрица ниже описывает оставшийся фактический
контракт, а НЕ обещание, что каждая строка уже восстанавливается повсюду.

### Все зарегистрированные профили Windows

Обозначения: R — есть app replay с readback; S — кнопка сохраняет, но replay нет;
O — состояние ОС/прошивки, собственного файлового сохранения нет; P — общие
preferences приложения; B — текущее состояние Windows, replay запрещён;
— — подтверждённый аппаратный backend не подключён. Звёздочка — условия ниже.
Доступность системных кнопок дополнительно зависит от обнаружения устройства.

| Девайс | Win Lock | Fn | USB | AC | Wi-Fi | Bluetooth | Камера | Тачпад | Микрофон | OSD | Copilot |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WUJIE X5SP4NAG | R* | R* | R* | R* | R* | B | R* | R* | O | P | P |
| GM6IR0C | R | R | R | R | R** | B | R** | R** | O | P | P |
| GM5HG0A | R | R | R | R | S | B | S | S | O | P | P |
| MACHENIKE L16 | R | R | R | R | S | B | S | S | O | P | P |
| Eluktronics HYDROC-16 G2 | R | R | R | R | S | B | S | S | O | P | P |
| HONOR/HUAWEI HLYL-WXX9 | — | O | — | — | S | B | S | S | O | P | P |
| MINISFORUM HPBSD | — | — | — | — | S | B | S | S | O | P | P |
| GM6PX7X | — | — | — | — | S | B | S | S | O | P | P |
| KUANGSHI-JIGUANG X6xx55xW-B2 | — | — | — | — | S | B | S | S | O | P | P |
| CANGLONG X6xR55xY | — | — | — | — | S | B | S | S | O | P | P |
| GM6BG0X | — | — | — | — | S | B | S | S | O | P | P |
| KUANGSHI-JIGUANG X6xx57TW | — | — | — | — | S | B | S | S | O | P | P |
| JIAOLONG X6DR55xx-B2 | — | — | — | — | S | B | S | S | O | P | P |
| GX6HG0U-F | — | — | — | — | S | B | S | S | O | P | P |
| CANGLONG X8xR55xP-B2 | — | — | — | — | S | B | S | S | O | P | P |
| YAOSHI X6AR55xY | — | — | — | — | S | B | S | S | O | P | P |
| GM6IX9B | — | — | — | — | S | B | S | S | O | P | P |
| Unknown actual device | — | — | — | — | S | B | S | S | O | P | P |

Последние10модельных профилей — inventory evidence only: отсутствие EC-команд
у них намеренное. Их Windows Wi-Fi/камера/тачпад/микрофон используют системные
адаптеры, а не неподтверждённые регистры другой платы. У предпросмотра ВСЕ команды
запрещены, сохранение выбранной виртуальной модели не выполняется.

R* Wujie: общий worker сначала получает DeviceOperation, ждёт свободный FAN
controller до60сек и проверяет EC writable. Поэтому даже системный Wi-Fi/тачпад
зависят от EC admission. Ошибки отдельных step обычно не останавливают следующие
строки, но ошибка общего capture/transport/lighting pause до step может остановить
весь проход. Повтор whole pass при таком исключении автоматически не планируется.

R** GM6: системные строки находятся в restoreConfigurationTick ПОСЛЕ профиля
мощности и FANs. Если сохранён Custom/Economy и AC не подтверждён — return до
системных строк; они ждут AC. Native subscription/OEM admission или исключение
APU/FAN до них тоже блокирует их применение. Это ошибочная зависимость разных
подсистем. Четыре quick EC строки имеют отдельный worker и этой AC-зависимости нет.

### Сон, гибернация, перезагрузка — конкретные точки входа Windows

| Событие | Wujie | GM6 | GM5/L16/HYDROC | Honor/HPBSD/inventory/unknown |
| --- | --- | --- | --- | --- |
| Запуск после reboot | startup → общий restore | startup → quick + keyboard/full config | startup → quick + static keyboard; HYDROC keyboard skip | Общие OSD/Copilot из app INI; system/ Fn replay отсутствует |
| PBT_APMSUSPEND | FAN suspend/отмена старого поколения | keyboard suspend/отмена проходов | тот же controller suspend | Общие объекты приложения остаются; KEYs replay не планируется |
| RESUMEAUTOMATIC / RESUMESUSPEND / RESUMECRITICAL | resumePower + power_resume | quick + keyboard/full config | quick + static keyboard; без system replay | Нет отдельного system/ Fn replay |
| Подтверждённый разрыв sleep clock | notifier FAN → system_sleep_clock | общий обработчик может вызвать restore, но отдельного GM6 clock notifier здесь нет | отдельного clock notifier нет | отдельного KEYs clock notifier нет |
| Смена AC/батареи, выключение экрана | Не является full KEYs restore; отдельная keyboard indication | Не является full KEYs restore; GM6 может закончить ожидавший AC full config | Не является system KEYs restore | Нет KEYs replay |

Hibernate не имеет отдельного KEYs-механизма: сохранённый процесс при получении
одного из перечисленных resume broadcasts идёт по той же ветке, что после сна.
После завершения процесса/перезагрузки действует только startup. USB/AC могут
быть важны ещё ДО запуска Windows: запись RAM-флага и readback не доказывают его
сохранность при обесточивании EC или постоянную BIOS/NVRAM-политику.
Для полного обещания S3/S4/S5 нужны отдельные аппаратные наблюдения каждой платы;
в этой ревизии сон/гибернация/reboot на устройствах не запускались.

OSD и Copilot не нуждаются в EC-перезаписи при обычном resume: их объекты и
preferences остаются в процессе. На новом запуске INI читается до их создания.
Copilot: WH_KEYBOARD_LL на F23, а не EC; ошибка установки hook попадает в startup
лог. Сохранённое true не доказывает, что hook удалось установить. Проверки
фактической блокировки клавиши после сна на всех машинах здесь нет.

### Полная Linux-матрица

Для каждого из17профилей и unknown: OSD=P; микрофон=O; Windows/Super lock,
USB, AC, Copilot, Wi-Fi, Bluetooth, камера и тачпад=—. Fn=O только для HLYL-WXX9
через Huawei sysfs, для остальных Fn=—. Предпросмотр не пишет ничего.
KEYs map/unmap запускает/останавливает read-only poll раз в3сек; это НЕ restore.
В GUI нет подписки KEYs на logind PrepareForSleep и нет startup replay Fn/mic.
Windows-обработчики питания не являются реализацией Linux-восстановления.

### Разрывы, оставшиеся после0.1.245

1. **System replay не общий.** Wi-Fi/камера/тачпад многих Windows устройств
   успешно сохраняются, но не читаются никаким lifecycle worker. Даже на GM6
   они привязаны к Custom/AC/FAN/APU, а на Wujie — к доступности EC. Нужен общий
   независимый путь с существующими checked setters; Wujie camera сохраняет свой
   EC-specific adapter. Перенос чужих регистров к Honor/HPBSD недопустим.
2. **Микрофон не сохраняется вообще.** В mapping строки10 стоит пустое optional,
   в DeviceSetting поля microphone нет; Windows/Linux меняют только текущий
   default endpoint. Rebind audio после resume восстанавливает наблюдение, не mute.
   Поэтому обещать app-восстановление mute после reboot/смены endpoint нельзя.
3. **Honor Fn не сохраняется в приложении.** Windows explicit skip в KEYs, Linux
   setter без INI; startup/resume replay не найден. Физическая firmware persistence
   может существовать, но из этих функций это не следует и на железе не проверено.
4. **Wujie неизвестный quick read пропускается.** differs требует saved И current;
   если чтение Fn/USB/AC/WinLock упало, команды нет, но пропуск отдельно не помечен
   failed. В конце может оказаться RESTORE settings confirmed. Исправление0.1.245
   устраняет ложный confirmed для system device, но не этот отдельный quick path.
5. **Нет одного итога по каждой KEYs-строке для каждого lifecycle.** GM6 quick
   worker независимо подтверждает свои4строки и имеет ограниченный retry до записи;
   общий settings_confirmed Wujie не является readback всех11строк. Для отсутствующего
   saved выбора и unsupported route должен быть skip с причиной, для unknown —
   unconfirmed. Windows-lock toggle после неоднозначного ответа повторять нельзя.

Физические/внешние изменения камеры, тачпада, Fn/WinLock сейчас отображаются,
но не записываются в app INI: observeState и poll не являются явным сохранением.
На Wujie/GM6 следующий replay может вернуть старый app выбор. Для Bluetooth это
специально запрещено новой политикой. Остальные внешние изменения нельзя молча
считать подтверждённым app-намерением, особенно при firmware reset после сна.

### Пути доказательств

- Реестр: src/devices/DeviceRegistry.cpp, mechrevo/ReportedProfiles.cpp и17Profile.cpp.
- Все11маршрутов: src/gui/common/KeysPolicy.h; UI save/readback:
  src/gui/windows/KeysPage.cpp:start/apply/complete/observeState.
- События: src/gui/windows/MainWindow.cpp:WM_POWERBROADCAST,
  kConfigurationRestoreMessage,create,restoreConfiguration.
- Wujie: ConfigurationRestoreWindows.cpp:capture/request/run;
  FanControlWindows.cpp:notifyConfigurationRestore и FanSleepTracker.
- GM: ControlWindows.cpp:restoreQuickSwitchTick,restoreConfigurationTick,
  restoreKeyboard,restoreQuickSwitches,setQuickSwitch.
- Флаги Uniwill: src/protocols/uniwill/QuickSwitch.cpp:readQuickSwitch/setQuickSwitch.
  Win:read768.0/command767.0; Fn:74E.4; USB:767.4; AC:726.3; update masks сохраняют
  соседние поля. Readback этих RAM-флагов не измеряет питание USB в S4/S5.
- Honor: hlyl-wxx9/ControlWindows.cpp:setFnLock; Linux HuaweiControlLinux.cpp:setFnLock.
- Системные адаптеры: WindowsDevices.cpp:setRadio/setSetupApi/setCamera/
  setTouchpadWindowsState; OsdSystemMonitor.cpp:bindAudio/watchTouchpad.
- Общие preferences: PortableSettings.cpp, CopilotKeyBlocker.cpp,
  Linux KeysPage.cpp:changeOsd/pollKeys, Linux OsdService.cpp.
- Архивные settings Honor/HPBSD от27.10.2026 в Users_logs: OSD/Copilot присутствуют;
  полей WindowsLocked/FnLocked/USB/AC/Wi-Fi/Bluetooth/camera/touchpad там нет.
  Это не доказательство потери: без команды пользователя отсутствие поля нормально.

Границы ревизии: статическая трассировка всех профилей и callbacks, реальные
локальные Wujie startup логи и ранее сохранённые Honor/HPBSD settings. Не выдаём
компиляцию или существование обработчика за аппаратное подтверждение S3/S4/S5.
