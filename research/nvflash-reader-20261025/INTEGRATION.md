# Статическая проверка и интеграция0.1.194

Продолжение REPORT.md: найден и добавлен Linux x64 reader той же5.867.0;
Windows исследован trusted Ghidra без исполнения импортированного файла.
Общий анализ ограничен240секундами и завершился timeout; postscript экспорт
выбранных функций выполнен. Это частичный статический разбор, не доказательство
всех внутренних эффектов NVFlash или аппаратной совместимости всех моделей.

Рассмотренные точки Windows:

- `1400a1820`: собственный embedded driver разворачивается через SystemDirectory,
  связанный service helper. Есть замена/удаление прежнего driver файла, поэтому
  product guard отказывает при существующих SYS/services до **каждой** команды.
- `1400a11b0`: альтернативная ветка CopyFile SYS из каталога EXE. Product
  оригинальный EXE не патчит; обе driver references остаются инертными `.bin`.
- `1400b5c90`, `1400b5440`, `1400b5290`: CreateServiceA, StartServiceA,
  DeleteService. Это lifecycle vendor tool, а не новый EC writer MagicControl.
- `140a470a0`: save firmware с отдельными результатами success/failure.
- `1401c8db0`/`1401ca400`: существуют ветки auto-disable display driver.
  Полная reachability этих веток для всех GPU/read modes не установлена;
  MagicControl передаёт только list/save и не добавляет disable/force/unload.
  Нельзя обещать отсутствие иных vendor side effects только по имени --save.

Первичный файл, хэши и подписи Windows: reference-evidence.json. Строки/
адреса: string-targets.json. Выборочные decompiled C/refs сохранены рядом;
Ghidra log фиксирует timeout и успешный экспорт. Исполнялся trusted анализатор,
а не NVFlash. Полного OEM byte-match unsigned EXE не найдено.

Linux архив с наблюдаемой страницы ComputerBase:
`nvflash_5.867_linux.zip`,27644184байта, SHA256
`73ab51e121129059f1377b3bf21a8001f6649d64af2471455499c803a9c80301`.
Member `x64/nvflash`: ELF64 x86_64,15909600байт, SHA256
`754693fb8f833be75601f95cb08a6e0cf538053cce84ab69020ec74ae96a6e70`.
Статическая версия string — NVIDIA Firmware Update Utility5.867.0.
Интерпретатор `/lib64/ld-linux-x86-64.so.2`; зависимости libdl/libm/libpthread/
libc. ELF stripped, без internal symtab; Linux-decompiler coverage не заявляется.
Ни version/help/list/save на железе не запускались.

В [первичном пользовательском отчёте NVIDIA](https://forums.developer.nvidia.com/t/nvflash/362577)
описан0-byte save RTX5090 на Linux5.867. Это не универсальный результат,
но подтверждает необходимость отвергать пустой файл даже при распознанных
метаданных. Product проверяет bytes/SHA/структуру независимо от exitcode.

Оба vendor payload хранят исходные байты и source provenance. Их собственные
embedded third-party license notices извлечены как текст и сохранены без
исполнения. Новых GitHub архивов и публикации нет.

UI/core контракт: docs/VBIOS-DUMP.md. При нескольких NVIDIA есть явный выбор,
адрес и все4 PCI IDs сравниваются с raw NVFlash list, index0 не предполагается.
После ошибок/отмены сохраняются raw/partial и метаданные. Только совпавшие
два непустых хэша и допустимый PCI ROM получают `saved=true`; физический SPI
полным не объявляется. Windows reuse runner сохраняет старые AFU/FPT argv.
Новое API не включает EC/ROM-ID/FAN/APU аппаратные действия.
