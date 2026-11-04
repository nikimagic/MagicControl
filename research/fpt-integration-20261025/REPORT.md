# BIOS reader и совместимость наших16 моделей,25.10.2026

Только анализ сохранённых файлов, source review и сборка. FPT/AFU/драйверы,
MagicControl и другие аппаратные reader помощником не запускались.

## Что именно означает «чип»

Процессор/PCH определяет Intel FPT или AMD-путь. Маркировка физической SPI NOR,
JEDEC-ID, напряжение и корпус — отдельные данные. Их нельзя вывести из перечня
поддерживаемых flash-чипов внутри утилиты. Во всех16 строках ниже точная модель
установленной SPI NOR пока неизвестна. Размер32МиБ GM6 получен из Descriptor
оригинальных образов; это не прочитанный с работающего чипа JEDEC-ID.
Wujie32МиБ — сохранённые два совпавших AFU capture + SMBIOS; модель NOR неизвестна.
Совместимость USB-программатора/прищепки и его напряжение этими данными не доказаны.

|Модель|Подтверждённый CPU|Программный reader и граница|
|---|---|---|
|WUJIE X5SP4NAG|Ryzen AI9 H365|AFU/AMI, сохранённый успешный32МиБ образ; FPT неприменим|
|Honor HLYL-WXX9|Ryzen5 4600H|FPT неприменим; AFU только после фактического AMI/SMBIOS допуска, успешного capture нет|
|GM6IR0C|i7-14650HX|FPT16.1: сохранённый LPC8086:7A0C входит в таблицу; ME16.1.30.2361,32МиБ Descriptor. Runtime чтение не проверено|
|GM5HG0A|Ryzen7 8845H|AMD/AMI → AFU; допуск по данным, успешного BIOS capture не показано|
|MACHENIKE L16 S|Ryzen9 7945HX|AMD/AMI → AFU; допуск по данным, успешного BIOS capture не показано|
|HYDROC-16 G2|Ultra9 275HX|FPT16.1 не допущен по CPU-имени/родству. Для этого поколения нужен отдельный точный PCH/CSME и reader|
|GM6PX7X|i9-13900HX|FPT16.1 кандидат; своего LPC/Descriptor в выбранном отчёте нет, проверяется реальный PnP|
|KUANGSHI X6xx55xW-B2|i5-14450HX|FPT16.1 кандидат; реальный LPC обязателен|
|CANGLONG X6xR55xY|Ryzen9 9955HX|AMD → AFU только при фактическом AMI и корректном SMBIOS; capture не проверен|
|GM6BG0X|Ryzen7 7435H|AMD → тот же guarded AFU, capture не проверен|
|JIGUANG X6xx57TW|i7-14650HX|FPT16.1 кандидат; нельзя переносить LPC другого ноутбука по CPU|
|JIAOLONG X6DR55xx-B2|Ryzen9 8940HX|AMD → тот же guarded AFU, capture не проверен|
|GX6HG0U-F|Ryzen7 8745H|AMD → тот же guarded AFU, capture не проверен|
|CANGLONG X8xR55xP-B2|Ryzen9 9955HX|AMD → тот же guarded AFU, capture не проверен|
|YAOSHI X6AR55xY|Ultra9 275HX|Та же граница что HYDROC: неизвестный/новый LPC блокирует встроенный16.1|
|GM6IX9B|i9-14900HX|FPT16.1 кандидат; требуется собственный LPC и успешный -I|

Итого9 AMD и7 Intel; из Intel5 конфигураций13/14HX и2 Ultra275HX.
Это не утверждение, что reader физически проверен на всех16 ноутбуках.
При просмотре чужого профиля применяется реальная идентичность ПК, не выбранная
строка меню. BIOS vendor не определяет семейство Intel SPI/FPT.

## Источники и реализация

- Сохранённые inventory: inventory-evidence.json / inventory-summary.json,
  получены audit_inventory.py без аппаратных вызовов. Краткие отчёты остальных
  моделей: research/laptop-reports/index.json и telegram-originals.
- [Оригинальное OEM происхождение](../fpt-official-origin-20261025/REPORT.md).
- [Статический разбор FPT/GM6](../intel-gm6-full-spi-20261025/REPORT.md),
  EVIDENCE.json содержит точную таблицу диапазонов LPC и все регионы.
- Дополнительный проверенный FUN14000E7A0(0) читает FLMSTR1 из самой SPI через
  FLMAP1/FMBA; именно этот master word используется известной FF-fill веткой
  region9 в FUN1400033B0. Это не показание live FRAP, поэтому свежий read fault
  всё равно должен завершиться ошибкой FPT; exit0 не заменяет structural checks.
- Original FPT EXE SHA25615d329ce4e5cbce10ad690e0727c54ffbbbb2a0489f80919c1b3b37858564aaa.
  Embedded driver reference SHA25674b3b7c559af70dca454486d49352bd251d68c43637b2898e4da8bcae29a447f.
  Обе Authenticode подписи проверены файловым Get-AuthenticodeSignature: Valid,
  Intel Corporation, thumbprint6AED8E3DB918ED7C2DC154F5F8E69F36A9CB2988.
  Kernel admission/работоспособность драйвера на ноутбуках не проверялись.
- Один существующий Windows collector расширен VendorTool; общий файл-only
  validator в platform/common/BiosImageValidation.h. Новых модельных сборщиков нет.
- Только фиксированные `-I`, `-d <newfile>` или AMD `/O`. FPT не запускается при чужой
  WNPDriver службе/SYS, включая остановленную службу. Наш код не удаляет её.
- Полнота и известная FF-fill ветка закрыты descriptor-host-read проверкой;
  extended regions12–15 остаются видимы, но их enabled layout не объявляется полным.
- На AMD сохранён прежний размерный контракт AFU, physical_spi_complete=null.
- stdout/stderr идут прямо в файлы без pipe backpressure, process timeout не
  вызывает принудительное завершение. Отмена завершает текущую vendor-операцию.
- UI меняет только строку статуса существующей кнопки. Linux аппаратный reader
  не подключён, общая геометрия/скролл не тронуты.

Локальный текстовый перечень оборудования по поручению владельца сохранён в
C:/Users/admin/Documents/mechrevo_dsh/Ноутбуки — характеристики.txt:16 моделей,
по одной строке с CPU/GPU/BIOS/EC/vendor/охлаждением/световыми зонами. Неизвестное
помечено явно, наличие зон не выводится из одних OEM Force* опций.

## Сборка и проверка файлов

Tools/Build-All.ps1 завершил общий Release0.1.190, обе рабочие portable обновлены.
verify_files.py не запускает приложение: сверяет526 входных файлов с manifest
сборки, версии и побайтное равенство built/portable, разбирает RT_RCDATA готового
Windows PE и сверяет все5 AMI и4 Intel ресурса с оригиналами. Результат:
[verification.json](verification.json). Тесты не добавлялись и не запускались.

- Windows SHA2562d8b2c5b98263fe60a0339c9d82524878c264864c3582136874f940f69014c57.
- Linux SHA2566fd1d4fd1fe7eba97e77f378b8d1302b2c430fa4c0bc5e85101bd755388090f7.

GitHub-архивы и публикация в эту задачу не входят. Личные settings/logs/дампы
не заменялись. Runtime/hardware чтение ещё требует пользовательского запуска
кнопки на подходящем Intel; помощник AFU/FPT не выполнял.
