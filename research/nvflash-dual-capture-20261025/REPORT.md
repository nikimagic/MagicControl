# VBIOS и основной EEPROM: подключение и офлайн-проверка25.10.2026

Изменение0.1.196 выполняет прямое поручение владельца: одна операция GUI/CLI
сохраняет обычный VBIOS и весь выбранный основной EEPROM через --entire,
каждый двумя чтениями. На железе ничего не запускалось. NVFlash, MagicControl,
mctl, драйверы, EC и GPU API помощником не выполнялись.

## Что означает «только чтение»

Общий типизированный allowlist содержит ровно List, Check, Save, SaveEntire,
Compare. Нативные Windows/Linux адаптеры строят argv из одной функции:

```text
--list
--index=N --check
--index=N --save FILE
--index=N --entire --save FILE
--index=N --compare FILE
```

Команд программирования, erase, protectoff, reset, override или произвольных
параметров от GUI/CLI нет. EC, ROM-ID, BIOS AFU/FPT этим collector не меняются.
Файлы результатов/логов создаются на диске; vendor может инициализировать GPU
и свой временный драйвер. Это чтение прошивки, не обещание отсутствия любых
временных изменений состояния ОС/GPU внутри proprietary NVFlash.

Точное исследование неизменённых Windows/Linux payload5.867.0:
[декомпиляция и доказательства](../nvflash-save-scope-20261025/REPORT.md).
В частности --entire выбирает offset0 и ёмкость descriptor, с внутренним
пределом16МиБ. --full не подставляется вместо --entire.

## Проверка результата

Ёмкость читается из распознанных EEPROM ID/part и uncapped Kxwidth у --check:
K * width *128байт. Например16384Kx1 и2048Kx8 означают2097152байт,
а2000KiB VBIOS — не весь такой чип. Копирование ROM до2МиБ не применяется.
Unknown,00/FF ID, противоречия, overflow и>16МиБ запрещают full, сохраняя
попытку обычного VBIOS. Перед каждым full-read и после compare ID/ёмкость
должны совпасть. Каждое чтение повторно привязывается к фактическим OS PCI
BDF, VEN/DEV/SUBVEN/SUBDEV и уникальному индексу NVFlash.

У каждой пары нужны непустые равные размеры/SHA256. У full — ещё точное
совпадение размера с ёмкостью и успешный --compare с положительным текстом
Firmware images are identical, без ошибок. Exit0 без текста недостаточен.
Обычный VBIOS проходит прежнюю55AA/PCIR/device/checksum проверку. Структура
full записывается отдельно: полный адресный образ может содержать повреждённую
прошивку. Bootability и независимое электрическое измерение не заявляются.

Два результата независимы; saved=true только если оба подтверждены. Ошибка
одного режима не удаляет успешный другой. Исключение native reader, смена
GPU/index или отмена прекращают дальнейшие операции. Никаких kill NVFlash.
Сохраняются manifest schema2, события с версией/UTC/action_id, исходные stdout/
stderr, версии/хэши tool, размеры/хэши файлов даже при отказе, причина partial.
CLI exit0 — оба результата, exit4 — partial/отказ. Имена обычных артефактов
pass1.rom/pass2.rom/vbios.rom сохранены; новые eeprom-pass1.rom,
eeprom-pass2.rom и eeprom-full.rom лежат рядом.

## Все модели текущего каталога

Ни одна строка ниже не означает проверку vendor reader на реальном ноутбуке.
Это проверка покрытия общей реализации, доступности и отказов. Реальная
совместимость определяется при явном запуске по фактической GPU/EEPROM.

| Модель | GPU каталога | Путь |
|---|---|---|
| WUJIE X5SP4NAG | AMD iGPU | NVFlash недоступен без фактической NVIDIA |
| Honor HLYL-WXX9 | AMD iGPU | NVFlash недоступен без фактической NVIDIA |
| GM6IR0C | RTX4060 Laptop | Общий NVIDIA collector |
| HYDROC-16 G2 | RTX5090 Laptop | Общий NVIDIA collector |
| GM5HG0A | RTX4060 Laptop | Общий NVIDIA collector |
| MACHENIKE L16 S | RTX5070Ti Laptop | Общий NVIDIA collector |
| GM6PX7X | RTX4070 Laptop | Общий NVIDIA collector |
| KUANGSHI X6xx55xW-B2 | RTX5060 Laptop | Общий NVIDIA collector |
| CANGLONG X6xR55xY | RTX5070Ti Laptop | Общий NVIDIA collector |
| GM6BG0X | RTX4060 Laptop | Общий NVIDIA collector |
| JIGUANG X6xx57TW | RTX5070Ti Laptop | Общий NVIDIA collector |
| JIAOLONG X6DR55xx-B2 | RTX5070 Laptop | Общий NVIDIA collector |
| GX6HG0U-F | RTX4060 Laptop | Общий NVIDIA collector |
| CANGLONG X8xR55xP-B2 | RTX5070 Laptop | Общий NVIDIA collector |
| YAOSHI X6AR55xY | RTX5070Ti Laptop | Общий NVIDIA collector |
| GM6IX9B | RTX4090 Laptop | Общий NVIDIA collector |

Итого16моделей,14NVIDIA,7семейств каталога. Иные комплектации той же модели
не подменяются preview: collector получает настоящие PCI IDs и EEPROM ID.
Unknown также может использовать фактическую NVIDIA; это не разрешает EC.
Выбор монитора/гибридный display route и AMD/Intel CPU в допуске не участвуют.

## Выполненные проверки

По отдельному поручению владельца «проверь для всех версий…» выполнены только
изолированные офлайн-проверки. Общий Build-All по-прежнему tests=OFF.

- Windows и Linux: по88проверок чистых parser/argv/catalogue, все PASS.
  Оба допустимых Kxпредставления, unknown/overflow/ambiguity/clamp, границы
  ёмкости, положительный compare, invalid enum/index/option filename,
  все14NVIDIA строк/7семейств и2интегрированные строки.
- Windows и Linux: реальный VbiosRomDump.cpp связан только с синтетическими
  OS/GPU/native stubs и системным SHA256. По9сценариев/337проверок, все PASS:
  оба результата; unknown EEPROM;32МиБ; обрезанный full; разные full SHA;
  молчащий exit0 compare; смена ID; pending reader; разные ordinary SHA при
  успешном full. Ни один native адаптер, vendor payload или GPU API не связан.
- Результаты: offline-windows.txt, offline-linux.txt, simulation-windows.txt,
  simulation-linux.txt. Синтетические ROM/manifest находятся только в build/.
- Общий билд/portable/resources/SHA: [verification.json](verification.json),
  сформированный file-only скриптом verify_files.py после завершения сборки.

AFU/FPT BIOS capture body и BiosImageValidation.h сверяются с0.1.193 отдельно.
Геометрия карточек/скролла и каталог устройств не менялись. Одна кнопка INFO
и прежний порядок driver → information → EC → BIOS → VBIOS сохранены.

Это не проверка работоспособности на14ноутбуках и не гарантия чтения всех
микросхем конкретной платы. Для подтверждения нужны пользовательские дампы
с ID/геометрией/raw ответами именно его GPU. Помощник аппаратные запуски не делал.
