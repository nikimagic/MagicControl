# Подписи встроенных BIOS/VBIOS reader-пакетов

**Уточнение после разбора multiple signatures:** сырой`/kp` выше проверяет
первую vendor-подпись,а не весь набор подписей. У AMI,FPT16,FPT19,NVFlash
иInsyde есть Microsoft secondary; **все шесть успешно прошли** отдельный
WinVerifyTrust DRIVER_ACTION_VERIFY и автоматический выбор этого provider.
Официальный NVIDIA Updater2.0 содержит ровно наши два SYS,свежий AMI архив
также совпал. Прежний общий вывод о непригодности драйверов не подтверждается.
Полный разбор:[причина `/kp` и официальные источники](../reader-kernel-trust-20261027/REPORT.md).
Исходные результаты отдельных команд ниже сохранены как evidence.

Проверено27.10.2026 на готовой portable0.1.245. Утилиты чтения и драйверы
не запускались/не загружались. Настройки доверия Windows не изменялись.

## Наличие и обычное доверие Authenticode

| Пакет | EXE | Драйверы и зависимости |
| --- | --- | --- |
| AMI AFU5.17.00.0153 | AFUWINx64.exe **NotSigned** | amigendrv64.sys:AMI US Holdings,Inc.;Valid;обе встроенные подписи проходят /pa |
| Intel FPT16.1.25.1871 | Intel Corporation;Valid;/pa0 | driver-reference.bin:Intel;Valid;обе подписи /pa0 |
| Intel FPT19.0.5.1948 | Intel Corporation;Valid;/pa0 | driver-reference.bin:Intel + Microsoft WHCP;Valid;/pa0 |
| Insyde H2OFFT6.76 | Insyde Software Corp.;Valid;/pa0 | H2OFFT64.sys,BiosImageProcx64.dll,FlsHook.exe подписаны Insyde;каталогH2OFFT.cat — Microsoft WHCP;Valid |
| NVIDIA NVFlash5.867.0 | nvflash64.exe **NotSigned** | Оба встроенных driver-reference имеют доверенную дополнительную подпись Microsoft WHCP;Valid;отдельная проверка signature1 /pa0 |

Итого проверены14 PE/CAT файлов:12 имеют подпись,2 EXE не подписаны.
FlsHook хранится как требуемая OEM signature dependency,но наш fixed-g его не
запускает. BIN references — инертные неизменённые образы драйверов,не loader.
Для их анализа созданы .sys копии с теми же SHA256,без запуска.

## Kernel-policy отдельно

Наличие подписи и Authenticode Valid не равны подтверждению допуска драйвера
ядром Windows. Повторяемые результаты сохранены вRESULTS.json иverification/.

- Insyde: `signtool verify /kp /c H2OFFT.cat H2OFFT64.sys` **успех0**;
  INF также подтверждён в каталоге через/pa /c. Отдельная vendor embedded
  подпись SYS без каталога не проходит/kp; каталог поставляется в runtime.
- AMI amigendrv64.sys: обычная/pa успешна,отдельная/kp **не прошла** —
  цепочка заканчивается корнем,не доверенным данной driver-policy.
- FPT16 reference: /pa успешна,/kp **не прошла** — certificate requested usage.
- FPT19 reference: /pa успешна,/kp **не прошла** для выбранной этим SDK цепочки.
- NVFlash references: /all /pa возвращает1 из-за одной из двух цепочек;
  дополнительная Microsoft подпись index1 отдельно проходит/pa0 у обоих.
  Отдельная/kp не прошла. Это не отсутствие подписей и не доказательство,
  что реальная загрузка Microsoft-подписанного secondary варианта невозможна.

Этот SDK не допускает/all,/ds,/ms вместе с/kp. Результаты неудачных проб ключей
не использованы как evidence отказа доверия. Использованы только корректные
команды изchecks вRESULTS.json. Целевая ОС хоста10.0.26300 записана как контекст;
это не аппаратный тест HVCI/Secure Boot/blocklist на каждом ноутбуке.
Загрузку драйверов для такой проверки помощник не выполнял.
Описание различий политик:[Microsoft SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).

## Проверено именно содержимое нашего приложения

inspect_embedded.py читает Windows PE через LOAD_LIBRARY_AS_DATAFILE:
код EXE/entry point не исполняется. В рабочем MagicControl.exe иmctl.exe
прочитано56 RCDATA ресурсов пяти пакетов; **56/56 совпали** с generated
EmbeddedRuntimeCatalog.h и проверенными исходными файлами побайтно поSHA256.
Это включает драйверы,каталог,INF,INI иprovenance,а не только имена EXE.
Подробности:EMBEDDED.json.

Сборка сверяет pinned SHA256 vendor-файлов; это контроль точных байтов,
а не цифровая подпись. В runtime BIOS reader проверяет driver WinVerifyTrust;
FPT/H2OFFT также проверяют EXE. У AFU неподписанный EXE известен заранее,
его подлинность ограничена pinned hash/provenance; фиктивное Valid не выдаётся.
NVFlash preflight фиксирует exe_signed=false,проверяет pinned hash и доверие
обоих reference drivers. Обычный WinVerifyTrust в приложении явно помечен
как cache-only,not kernel admission proof.

Linux NVFlash — ELF,Windows Authenticode неприменим. Его SHA256 и размер
зафиксированы вEMBEDDED.json; не выдаём checksum за подпись. Windows BIOS
AFU/FPT/H2OFFT пакеты не запускаются из Linux. Чтение AMD VFCT не является
запуском пакета AMDVBFlash и отдельно не проверялось этим audit.

## Файлы результата

- Audit-Signatures.ps1:воспроизводимая пассивная проверка14 файлов.
- RESULTS.json:SHA256,сертификаты,сроки,TimeStamperCertificate,статусы икоманды.
- verification/:полные выводы signtool,включая цепочки ивремя подписания.
- inspect_embedded.py/EMBEDDED.json:проверка56 встроенных ресурсов GUI/CLI.
- Исходные PROVENANCE.json остаются неизменными вruntime каждого поставщика.

Vendor-пакеты не переподписывались,не патчились ине обновлялись этим audit.
