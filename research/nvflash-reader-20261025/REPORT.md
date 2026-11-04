# NVIDIA VBIOS reader: статическая проверка25.10.2026

Запрос: сначала найти и проверить NVFlash; позднее подключить отдельный дамп
видеокарты. Новое поручение INFO задаёт место пятой кнопки, но не отменяет
проверку reader. В0.1.193 кнопка VBIOS недоступна, с пояснением обеих ОС.
Сборщик/драйвер не запускаются, бинарники NVFlash в продукт не включены.

## Что проверено по самим файлам

Получен стандартный архив5.867 со страницы
[ComputerBase](https://www.computerbase.de/downloads/systemtools/grafikkarten/nvflash/).
Архив15621563байта, SHA256:
`e158da4a6f2138d2268452c6b5fa315c8fdebe827d9e556fa52e310c448c38d4`.

Его `nvflash64.exe` — PE32+ AMD64,23535616байт, File/ProductVersion5.867.0,
описание NVIDIA Firmware Update Utility, Authenticode **NotSigned**.
SHA256 `c0620a7be0ffdad3782d9ddf238d1a19c31b587cbcfee7036819c8a50e2d89cd`.
Копия `nvflash.exe` из
[фиксированной ревизии Blackwell-Bios-Editor](https://github.com/bmgjet/Blackwell-Bios-Editor/blob/cc118df628fcb58146edec506ef3b13342d3c6e5/nvflash.exe)
побайтно совпадает с этим x64-файлом. Имя без64 само по себе не определяет
архитектуру. Совпадение двух зеркал не заменяет OEM provenance/подпись EXE.

Внутри x64-файла найдены два самостоятельных PE-драйвера NVIDIA Flash Driver
Version1.21.0. Для обоих Get-AuthenticodeSignature вернул Valid, signer —
Microsoft Windows Hardware Compatibility Publisher. Ничего не загружалось:
это проверка извлечённых файлов с инертным расширением `.reference.bin`.

| Raw offset EXE | Размер | SHA256 драйвера |
|---|---:|---|
|0x1250DD0|45776|79050454d06ab7b6607bca09386f704df021c3c367cc0743de6845301bb192ac|
|0x125C0A0|48848|8c2d9847b70c3dc1aac5d9e5330c67deb86f369dcd3ca85845adf2a001088dde|

В строках самого EXE присутствуют help-формы `--list`, `--save <filename>`,
`--index=1`, имена `nvflash.sys`/`nvflsh64.sys` и сообщения об установке
драйвера. Есть и сообщения об автоматическом отключении display driver,
поэтому отсутствие записи firmware не доказывает отсутствие других эффектов.
До интеграции требуется проследить конкретный путь чтения и lifecycle.

Папка первичных файлов вне репозитория:
`C:\Users\admin\Documents\mechrevo_dsh\analysis\nvflash-research-20261025`.
В ней сохранены архив, две независимые копии EXE, инертные SYS reference,
`archive-members.json`, `embedded-pe-inspection.json`, `nvflash-strings.json`,
страница/квитанция скачивания. Подписи/версии записаны в `reference-evidence.json`
рядом с этим отчётом. Ни `--help`, ни `--list`, ни `--version` не запускались.

## Что пока не доказано

Это найденный кандидат5.867, а не подтверждённая поддержка всех RTX40/50.
Нужны точное происхождение оригинального EXE (желательно совпадение с OEM
пакетом), статическая проверка read-only команд/driver lifecycle и проверки
выбора физического адаптера по PCI. Патченные nvflashk/nvflashb с обходом
проверок не выбраны. Не требуется отключать защиту Windows.

Приведённая пользователем
[тема форума NVIDIA](https://forums.developer.nvidia.com/t/request-gpu-memory-junction-temperature-via-nvidia-smi-or-nvml-api/168346?page=14)
содержит справку Linux5.660.0, опубликованную в2021году. Она подтверждает
исторический синтаксис сохранения, а не Windows5.867/Blackwell совместимость.
Просмотр текущего указателя NVIDIA Product Security не дал отдельного
NVFlash advisory; это не утверждение об отсутствии уязвимостей.

Будущий collector должен получить PCI identity → точное сопоставление с
`--list` → два `--index=N --save` → raw stdout/stderr/exitcode → размеры,
SHA256 и проверку структуры55AA/PCIR/PCI IDs. Не предполагать index0, не брать
GPU по выходу монитора и не объявлять сохранённый VBIOS полным физическим SPI
или гарантией восстановления. Ошибки/частичные файлы сохранять в отчёте.

## UI границы

Изучен эталон BIOS scrollbar `d84343d`. В INFO меняется только существующая
первая строка: драйвер → сведения → EC → BIOS → VBIOS. Windows/Linux используют
общие данные порядка и geometry; Tab следует тому же порядку. Padding12,
gap6, высота30DIP и вертикальные формулы остальных строк не менялись.
Новые callbacks управления аппаратурой не добавлены. Сборка/стейджинг
проверяются отдельно без запуска GUI, CLI, collectors или аппаратных тестов.
