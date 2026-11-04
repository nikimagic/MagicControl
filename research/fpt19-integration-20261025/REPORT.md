# Подключение FPT19,25.10.2026

По прямому поручению владельца: подключить FPT19.0.5.1948 только для фактических
HYDROC/YAOSHI с Ultra275HX, сохранить AFU/FPT16, не запускать на железе,
собрать Windows/Linux, обновить рабочие portable и сделать локальный коммит.

## Источник и отдельный runtime

Официальная страница https://www.colorful.cn/igamecenter/ вернула публичную
ссылку Colorful iGameCenter2.0.0.82:
https://download.colorful.cn/iGameCenter/iGameCenter-2.0.0.82-2026.09.12-Installer_Prod.exe

SHA архива ce7f0b697dbc64662541838aed2d914508bb085db4b8e6186749b0dd4a4f8c53.
Member app/iGameAPI/Notebook/NotebookSDK/N15_25/FPT/FPTW64_8102.exe.
Распакован архиватором без запуска инсталлятора; исходные байты сохранены.

|Объект|Версия|Размер|SHA256|
|---|---|---|---|
|FPTW64.exe|19.0.5.1948|2841008|88491119862830235854cd29b9857de21870ac4dc16f74544b1a665f64d62807|
|driver-reference.bin, RT_RCDATA118/1033|2508.0.23.0|46368|acd5d366a73e9d60c654f9b6d76397d0439606939d6b0c7ee24c4f397452899a|

Подписи файлов проверены offline: EXE Valid/Intel Corporation, драйвер
Valid/Microsoft Windows Hardware Compatibility Publisher. Драйверный .bin
нужен только для WinTrust evidence; vendor извлекает собственный embedded SYS.
Исходные файлы встроены отдельной группой IntelFpt19; прежние IntelFpt/AmiAfu
не заменяются. Runtime cache, ACL/проверка SHA/pinning и stager общие.

## Допуск без EC

1. Читаются registry SystemManufacturer/BaseBoardProduct/SystemProductName.
2. Существующий чистый deviceProfile(BoardDescriptor) определяет реальную
   модель. Его список не выполняет probes/EC. UI preview не используется.
3. Только HYDROC-16 G2 либо YAOSHI Series-X6AR55xY, GenuineIntel и brand,
   нормализующийся в suffix ULTRA9275HX, выбирают кандидата19.
4. Один actual present PCI LPC/eSPI class0601 через SetupAPI; допустимы
  7F00–7F7F и7700–777F точной таблицы19. Затем FPT -I должен вернуть0.
5. Только после проверок, mutex/AC/driver/signatures выполняются два фиксированных
   -d в новые файлы. Перехода на16/AFU после отказа19 нет.

Остальные Intel сохраняют прежнюю проверку16:7A00–7AFF/5180–51FF/5480–54FF.
AMD/AMI сохраняет AFU5.17.00.0153 /O. Capabilities YAOSHI, EC-карты, FAN/APU,
батарея, ROM-ID, подсветка и внешний вид приложения не меняются.

## Статический разбор exact19

Ghidra/JVM и Capstone исполнялись только как анализаторы сохранённого PE.
Исходный FPT/SYS/installer/MagicControl помощником не запускались.

- Таблица VA1402A0160, strideD0, используется14003C330: сверяются Intel8086,
  internal platform и low/high LPC.7F00 сохраняет подпись Intel Meteor Lake
  Platform/internal1,7700–777F — Arrow Lake Platform/internal0. Имена не
  переименовываются под маркетинговый CPU.
-140017DB0 открывает \\.\IntelCseWNP, может остановить старый WNPDriver и
  вызвать создание службы.140018680 может удалить существующую службу.
  Поэтому общий guard проверяет её отсутствие даже в stopped состоянии.
-140018370 извлекает resource118, пишет Windows/System32/Drivers/WNPDriver.sys;
  fallback — environment SystemTemp/WNPDriver.sys. Может снять read-only и
  удалить существующий SYS. Новый guard проверяет и этот дополнительный путь,
  запрещает относительный SystemTemp. Наш collector чужие файлы не удаляет.
-140017FA0 закрывает handle, останавливает/удаляет свою WNPDriver и вызывает
 140018090 cleanup. Если после процесса остался driver/service, следующий
  проход не запускается. Принудительное завершение vendor-процесса не добавлено.
-140003790 заполняет буферFF и выполняет full либо region read. В региональной
  ветке при ошибке выводится "Skipping overlapping data from region:" с
  "Read failed with error", пропуск заполняется00, затем возможен успешный
  save. Для19 raw stdout/stderr с этими маркерами или запросом truncation
  запрещают признать полный SPI даже при exit0/совпадении двух файлов.

Общие Descriptor/density/16-region coverage/read-permission/FV/ME и два SHA
проверяются прежним валидатором. Extended12–15 layout остаётся недопущенным.
Raw/partial, исходный precheck, candidate version/group, tool provenance,
каждый ответ/ошибка/файл/хэш сохраняются. Проверка живых чтений не выполнена;
подключение reader не является обещанием доступа ко всем регионам любой BIOS.

## Воспроизводимость

inspect_static.py проверяет pinned SHA и сохраняет static-evidence.json.
decompile.py запускает доверенный Ghidra на saved PE, never hardware.
prepare_runtime.py копирует неизменённые pinned EXE/reference и provenance.
Build-All завершил Windows/Linux0.1.191 и обновил обе рабочие portable.
verification.json подтверждает530 build inputs, версии/SHA и побайтное равенство
обеих portable сборкам, все13 оригинальных AMI/FPT16/FPT19 PE resources/notices.
audit-evidence.json отдельно сверяет с dcfff9e фиксированный vendor runner,
signature check, AFU/FPT16 спецификации, полный image validator,9 прежних ресурсов
и неизменность src/gui. Это файловое сравнение, не runtime-тест оборудования.
Аппаратные тесты не разрешены;
программные тесты без отдельного запроса по AGENTS.md не добавляются/не запускаются.

Windows SHA25695861efdd401a7671aef04b19cc2ba411bedbfe4f0e697c09db70746811854f7.
Linux SHA2568e13561d94f73d259624a38c14447872c5f0493a2fa0c44f9f2a0e10df48bce3.

Дополнительная перепроверка утверждений владельца: [CLAIMS-RECHECK.md](CLAIMS-RECHECK.md).
