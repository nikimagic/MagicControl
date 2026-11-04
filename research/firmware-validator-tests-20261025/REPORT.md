# BIOS/VBIOS валидаторы: проверка25.10.2026

Прямое поручение владельца: тесты AFU/FPT16/FPT19 и NVIDIA, синтетика и наши
дампы, не запускать читалки/железо, сохранить рабочие reader и сделать коммит.
Уточнение владельца: реальные образы есть только WUJIE и GM6, не GM5.

## Реальные файлы и обнаруженная ошибка

Источник и полные SHA зафиксированы в
[real-files.json](../../tests/offline/firmware/real-files.json). Сами личные
BIOS/ZIP в коммит не включены. Читались только существующие файлы:

| Файл | Размер | Ожидание |
|---|---:|---|
| WUJIE AFU bios.bin, capture193321,0.1.186 |32МиБ| declared_rom_image |
| WUJIE bios-verify.bin той же пары |32МиБ| тот же SHA и declared_rom_image |
| GM6 backup.zip/backup.fd, stock |32МиБ| intel_spi,32МиБ Descriptor,18FV |
| GM6 new.zip/new.fd, patched |32МиБ| та же структурная форма,18FV |
| GM6 logs(15)/pass1.partial.bin |16МиБ| отказ как fullSPI: нет Descriptor |

WUJIE проверялся как AFU declared image: его сохранённый manifest даёт
33554432байта и два равных SHA. Это не независимое измерение физической SPI.
GM6 full-файлы — существующие пользовательские образы, не новый FPT capture
и не свидетельство успешного чтения нашим FPT16/FPT19. Патченый образ не
объявляется заводским или рекомендованным для прошивки.

Первый тест реального GM6 выявил ложный отказ прежнего validator. Descriptor,
карта регионов и header checksum всех18FV корректно разбираются, но reset
stub у обоих образов имеет90 90 E9 A3 AE...: два NOP перед переходом.
Старый код рассматривал лишь opcode0/1 и не видел E9 на позиции2.

Минимальная production-правка: чистый resetJump(span) сохраняет прежний допуск
opcode0/1 и дополнительно пропускает ведущие90; затем требует целую E9 rel16
или EA ptr16:16 инструкцию внутри последних16байт. Произвольного поиска E9
в данных нет. Пустой/короткий/весьNOP/усечённый переход отвергаются. Это
распознавание ограниченной формы, не x86 эмуляция и не доказательство загрузки.
Единственный изменённый production-файл — BiosImageValidation.h.

Первоначальный запуск также выявил ошибку конструкции synthetic extended-region
fixture: после сдвига начала BIOS первый FV остался вне региона. Fixture
исправлен размещением двух FV внутри BIOS; guard extended12 остаётся отказом
allRegionsReadable. Это не приписывается дефекту reader. Исходный протокол
первых отказов сохранён в initial-failures-windows.txt.

## Покрытие и границы

Suite зарегистрирована в tests/offline/firmware и через родительскую offline
CMake. Есть отдельные CTest AFU, FPT16, FPT19, NVIDIA, real files, а также
переиспользованные NVIDIA parser/catalogue и collector simulation.
[Подробные случаи и команды](../../tests/offline/firmware/README.md).

На каждой ОС: AFU35, FPT16 105, FPT19 105, NVIDIA PCI ROM/capacity/argv72,
прежний NVIDIA contract88, collector simulation337assertions/9сценариев.
Всего742проверки C++ плюс5файловых fixtures в Python. Это число assertions,
а не742независимых моделей или физических испытания. Все7CTest-групп должны
быть PASS, без SKIP в локальном запуске с имеющимися файлами.

Раздельные FPT16/FPT19 случаи проверяют их собственные LPC/CPU/profile guards,
а разбор IFD2 используют общий. Для обоих протестированы1/2компонента.
FPT19 не объявляется отдельным «универсальным форматом SPI». Реального дампа
Ultra275HX/FPT19 в выбранном наборе нет. Реальных NVFlash ROM/entire capture
также нет: NVIDIA проверяется синтетикой,14профилями/7семействами каталога и
реальным collector с mock OS/GPU/vendor, без настоящего процесса NVFlash.

Checksum FV относится к его заголовку. Изменение payload не должно ошибочно
объявляться нарушением header checksum. Legacy PCI ROM checksum проверяется
отдельно от EFI. VBIOS2000KiB не считается fullEEPROM2МиБ. Ёмкость NVIDIA
проверяется по uncapped descriptor, не по файлу. Для full нужны два точных
чтения/SHA и положительный raw compare. Файлы сохраняются при partial.

Gaps, minimum2FV и форма reset — консервативные admission rules текущего
decoder, а не универсальные доказательства повреждения. Распознанная структура
и полнота диапазона не доказывают bootability, recoverability или физическую
работоспособность нашего reader на соответствующем ноутбуке.

## Исполнение и сохранность

Выполнены Release offline tests Windows/MSVC и Linux/GCC15. Дополнительно
5файловых/буферных CTest Linux под AddressSanitizer+UBSan. Подменять тестовые
отказы ожидаемыми успешными results или отключать san-checks не требуется.
Результаты: windows-tests.txt, linux-tests.txt, linux-sanitize-tests.txt,
real-files-windows.json, real-files-linux.json; финальная проверка сборки
и неизменности runtime/reader в verification.json.

Ни один vendor executable, driver, GUI/mctl entry point, EC/GPU/ACPI/NVML API
не запускался. Только компилятор, тестовые executable с чистыми header-функциями
или синтетическими stubs, Python, системный SHA256 и работа с файлами.
Копии fixtures создаются только в build/. Original images остаются прежними.

Product0.1.197 собрана Build-All tests=OFF, обе рабочие portable GUI/CLI
обновлены; версии/SHA всех4бинарников совпали. Все553application inputs и
47исходных embedded resources каждой Windows GUI/CLI проверены файлами.
Это необходимо, чтобы исправление resetJump
попало в рабочий бинарник, а не осталось только в тестах. AFU/FPT/NVFlash argv,
native runner/driver guards, runtime payloads/signatures, UI/карточки/скролл
не менялись. GitHub архивы и публикация не выполняются.
