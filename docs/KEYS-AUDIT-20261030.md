# KEYs: ревизия всех11кнопок,30.10.2026

Таблица ниже описывает текущую реализацию394, а не полный предел возможностей
прошивки. Дополнительный статический разбор Wi-Fi/Bluetooth/тачпада/микрофона:
`research/keys-ec-vs-os-20261030/REPORT.md`. Новые пути Wi-Fi/Bluetooth/тачпада
не подключены. В394 подключено зеркалирование microphone mute в LED flag,
а не аппаратное отключение микрофона; физическое свечение LED не наблюдается.

Рабочий контракт после удаления отдельного Copilot blocker. Это ревизия кода,
а не физический прогон11действий на каждом компьютере. Профиль в меню не
даёт доступа к чужой EC-карте. Вся вкладка остаётся доступной и на неизвестном
компьютере; отдельный неподдержанный аппаратный переключатель недоступен.

| Кнопка | Фактическое чтение / команда | Сохранение и lifecycle |
|---|---|---|
|Windows Lock|подтверждённый Uniwill768.0;767.0 — однократная команда с readback, не прямой status setter|KEYs файл; общий restore, toggle не повторяется после неоднозначного ACK|
|Отключить OSD|состояние приложения, не EC|KEYs файл; OSD callback, без native command|
|Fn Lock|Uniwill74E.4 либо родной Honor HWMI|KEYs файл; общий reader/setter/restore|
|USB при выключении/гибернации|Uniwill767.4 с сохранением RGB/lightbar битов|KEYs файл; общий restore; физическое питание отдельно от readback|
|Автовключение от адаптера|допущенный Uniwill fallback726.3|KEYs файл; общий restore; аппаратный запуск проверяется отдельно после power cycle|
|Copilot Lock|свежий native getter; Wujie728.2 +741.0, другие устройства только свой подтверждённый typed EC/OEM keyboard ABI|KEYs файл; общий ресурс copilot-lock и KeysLifecycle; нет hook/GUI replay/5сек watchdog|
|Wi-Fi|Windows radio API, fresh state после setter|KEYs файл; общий restore; разрешение радио готовится на GUI STA только для действия пользователя|
|Bluetooth|Windows radio API|KEYs файл хранит выбор; lifecycle сохраняет текущее состояние ОС, не включает Bluetooth при startup/resume|
|Камера|на точном Wujie EC727 с маской18; иначе штатный Windows device backend|KEYs файл; общий restore. CLI теперь выбирает тот же Wujie backend, что GUI|
|Тачпад|Windows Precision Touchpad / SetupAPI по прежнему native пути|KEYs файл; общий restore; F24 OSD observer не менялся|
|Микрофон|Windows audio endpoint; на точном Wujie mute дополнительно зеркалится в LED flag7A6.2; Honor/Linux сохраняют прежний backend|KEYs файл после fresh readback ОС; общий restore и event-driven observed sync, без отдельного таймера|

## Исправленные расхождения

1. Copilot убран из ApplicationPreferences и LocalPreference plan. GUI больше
   не подставляет сохранённый bool вместо аппаратного результата. Обе старые
   CopilotKeyBlocker исходные единицы удалены, CMake больше их не собирает.
2. MainWindow больше не содержит Copilot объекта, startup setter, preference
   rollback setter, отдельного resume setter и пятисекундного sync.
3. Одна аппаратная операция через applyKeyHardware/ресурс copilot-lock
   используется GUI, CLI и KeysLifecycle. Выбор транспорта внутри адаптера не
   запускает несколько blocker-ов: exact EC/typed EC выбирается по admission,
   другая машина может использовать source-verified OEM WMI keyboard ABI.
   Программного fallback нет. Неподдержанные машины не показывают фиктивную
   активную блокировку из сохранённого файла.
4. Native effective Off может скрывать latched728.2 при APP0. Адаптер требует
   одного idempotent reconcile на lifecycle; saved Off очищает728.2 независимо
   от APP presence. Сам741.0 при Off не снимается, FAN/RAM/PL не меняются.
5. Поддержанные команды KEYs в CLI теперь проходят executeKeyHardwareCommand
   с тем же подтверждением/сохранением, что GUI. Windows camera использует
   актуальный Wujie context; Linux Fn/microphone — executeKeyOperation, как GUI.
6. Legacy import и CopilotBlocked в keys-settings.ini сохранены. Schema/version
   не менялись. Linux не получает непроверенный Uniwill/Copilot writer;
   сохранённый Windows Copilot выбор не имитируется как Linux hardware state.

## Что осталось прежним

Все остальные native register masks, Win Lock settling/one-shot semantics,
радио/Bluetooth policy, microphone readback, unknown/preview guards, раскладка,
шрифты, OSD service иF24 observer сохранены. KEYs visible poll остаётся2сек,
действия и lifecycle идут в существующих workers с DeviceOperation admission.
Пятисекундный tray timer сохраняется для других задач, но Copilot из него удалён.

Обновлены только ожидания существующих projection/policy tests под новый
контракт; наборы автотестов и аппаратные переключения не запускались.
После сборки393 выполнены10CLI status-команд с --read-only на Wujie:
Windows/Fn/USB/AC/Copilot/Wi-Fi/Bluetooth/Camera/Touchpad/Microphone, все exit0.
OSD проверен как сохранённое программное предпочтение. Это проверка getters,
а не физическое переключение всех11кнопок и не sleep-test всех устройств.
Пользователь подтвердил физическую клавишу на393: при On короткое нажатие и
удержание5сек не открывают Copilot, при Off клавиша работает; финально On.
Лог393 подтверждает native Off08:19:05МСК иOn08:19:17МСК с readback.
Это не sleep/hibernation-test и не физическая проверка всех устройств.
