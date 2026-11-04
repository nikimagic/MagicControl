# NVFlash: private hidden console, только чтение

Основание: Users_logs/logs_GM6IR0C(17).zip,0.1.204,18:10:38–39МСК.
--list прошёл, PCI10DE:28A0/SUBSYS139A1D05/BDF0000:01:00.0 привязан к index0.
--check вывел Cannot find console window; exit/result не зафиксированы.
Следующие попытки встретили service nvflash,state4.

- CREATE_NO_WINDOW заменён **только для NVFlash** на CREATE_NEW_CONSOLE.
  STARTF_USESHOWWINDOW/SW_HIDE сохранены: консоль скрыта при создании, parent
  console не меняется. Один путь используется GUI/CLI и всеми read-командами.
- Набор argv не меняется. Это list/check/save/entire-save/compare, никакой
  прошивки/protectoff/reset/отключения display driver или автоподтверждения.
  stdin остаётся NUL; исходные vendor bytes и подписи/pins прежние.
- Прозрачный event записывает private_console/show_window/creation_flags и
  вывод stdout/stderr. FPT/AFU не получают новых флагов и не меняют команды.
- Обнаружение старой console-error работает по обоим raw-потокам и в CLI без
  GUI binding. Rolling tail позволяет распознать разделённую между chunks строку.
  Явный отказ сохраняет raw/PID/pending; нет слепого повтора или TerminateProcess.
- Существующая служба по-прежнему блокирует новый reader. SCM читается только
  QUERY_CONFIG/ENUMERATE; в collision добавлены image/start type/error и статус.
  Stopped registration не выдаётся за живой driver. Stop/DeleteService не добавлены.

API-основания и границы описаны в docs/VBIOS-DUMP.md. Подтверждённое исправление
на конкретной RTX требует пользовательского повторного дампа. Уже оставшийся
внешний процесс/служба не изменяются агентом. Перезагрузка выгружает driver,
но если registration сохранится, новый лог даст stopped/active и точный путь.

Проверка: compile GUI/mctl обеих ОС, portable version/SHA, static diff read
allowlist и embedded bytes. Никакой продукт, OEM/NVFlash binary, тест с GPU,
драйвер, EC/SMU/HID action или аппаратный read агентом не запускается.
Linux reader/session/read protocol не меняется. Архивы GitHub не запрошены.
