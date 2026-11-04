# Восстановление CLI,0.1.192

По прямому поручению владельца25.10.2026 восстановлена console/SSH версия.
Решение заменяет запрет mctl от24.10.2026; AGENTS.md актуализирован.

Git-история:0fb38b4 удалил src/cli и исключил mctl из portable. До него
CLI838строк содержал старые прямые EC/полосочные записи. Автопроверка отклонила
первоначальное подключение такого источника к сборке. Подключение не обходилось:
исторический main заменён текущими typed adapters без writeVerified/updateBits/
DeviceIoControl/raw writer. После этого безопасный вариант включён штатно.

CLI и GUI используют существующие model factories, guards, реальные источники
мониторинга, все16 записей реестра, typed FAN/power/battery/keyboard/HID/KEYs/GPU,
общий inventory/EC dump и автоматический BIOS collector0.1.191. Неподдерживаемая
функция/ОС остаётся отказом. Исторические raw reg/lightbar write команды не
восстановлены. Подробный покрываемый синтаксис и ограничения: docs/CLI.md.

Особенности SSH:

- Unicode Windows wmain, UTF-8 stdout JSON/JSONL, stderr и exit codes.
- CLI console subsystem3/asInvoker: нет UAC/GUI диалогов; нужны права сеанса.
- Help/version возвращаются до runtime/controllers/startup ROM. Нет GTK init.
- Явный --yes для операций изменения, dry-run аппаратные API не конструирует.
- Общая instance lease с GUI для Windows writer, read-only сосуществует с GUI.
- Не выполняется общий startup restore. Gm6Controller принимает startupQuickRestore:
  CLI false, прежний GUI default true; значение задаётся до старта worker.
- Foreground host/colour/Honor watchdog не держит interactive DeviceOperation
  весь цикл, позволяя существующему background controller работать.
- Нет новых exit Default resets: используются прежние деструкторы core.
- SingleZone и typed native guards не распространяются на чужие модели.
- GUI/CLI FAN Up/Down/PWM используют один codec customCurvesFromFanProfiles.
- GUI меняет лишь ссылку на shared instance name и вызов того же codec;
  шапка, карточки, отступы, размеры и скролл не изменены.
- BIOS reader и image validator побайтно совпадают с0.1.191.

## Общая сборка и файлы

Build-All.ps1 теперь компилирует magiccontrol-gui + magiccontrol(mctl) обеих ОС,
stager копирует оба, session metadata проверяет gui/cli SHA и общий номер.
Завершён0.1.192; обе рабочие portable обновлены.536 application inputs совпали
с build manifest. Все41 оригинальные embedded runtime entry проверены в обоих
Windows PE, включая PawnIO/Uniwill/AMI/FPT16/FPT19. Версии PE/ELF и все4 хэша
сверены, built/portable побайтно совпали. Receipt: verification.json.

|Бинарник|SHA256|
|---|---|
|Windows GUI|7b8e244a4c548bf9d9375ec69a97b6faa064b95eefd89ba265993b3da193551b|
|Windows mctl|002e428c8f2563883ec83a6ddfbbf09cc40bd2d3e7cbd4f92229816481297c18|
|Linux GUI|5e955c193d30829de5436c2e7bcb04b80b2e527698867e0dce8efedf91ccfaa6|
|Linux mctl|afb18b76134f9abd7a909b8fbd97cc011fb00ee571bf4ef5e1e81304b5bdb5c9|

Приложение, FPT/AFU/SYS и hardware commands помощником не запускались.
Новых тестов нет и они не запускались по AGENTS.md. Реальное SSH-подключение/
физическое применение команд не проверено; успешная компиляция не заменяет его.
Личные настройки/логи сохранены вне коммита. GitHub-архивы/публикация не выполнялись.
