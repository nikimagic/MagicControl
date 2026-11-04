# Дополнение: MRO10 содержит EC1.12, совпадающий с live identity/ABI

22.10.2026. Предыдущий [BIOS/SMM отчёт](REPORT.md) о MRO09/EC1.11 **не
переписан**. Это новый источник. CAP, SPI и EC не исполнялись/не прошивались.

## Новый бинарный источник

`build/gm6-battery-firmware-20261021/EC-MRO10-v1.12.bin`:
131072байта, SHA-256
`5659c8217e9d93d3fd6fe278756872d7c5adbd15a0687d35705e2df4bb5c05dd`.
Root также сохранил durable копию в firmware-sources; её manifest ведётся отдельно.

Самостоятельно проверено полное, единственное вхождение **всех131072байт**
этого EC в каждом из трёх32MiB SPI-файлов MRO10 archive:

|Файл|EC physical offset|EC SHA|
|---|---:|---|
|backup.fd|0x151D81C|5659c821…bb5c05dd|
|bios.bin|0x151C8FC|тот же|
|new.fd|0x151C1EC|тот же|

Полные SPI SHA, смещения, EC SHA и metadata —
[MRO10-EC112-identity.json](MRO10-EC112-identity.json). Это доказывает, что
BIOS CPU/Setup варианты внутри **данного архива** содержат одинаковый EC payload.
Не доказывается заводская аутентичность каждого owner-modified SPI или полное
равенство flash живого ноутбука — полный live EC flash hash не снимался.

## Family, version и Project25 подтверждены отдельно

Trusted UEFIExtract использован только с selected GUID и `-m body`, без полного
dump всех модулей. Из **backup.fd** получены:

- FID120байт: `GMxIR7xN1`, SHA
  `f5a15125ccd778ff2899566f9e90b3b380be9cac47848ef6f545cbca9b86da9f`.
- SGN118байт: `BIOS Date:12/19/2024 12:48:01 Ver:N.1.05 MRO10`, SHA
  `f09d4f71cf552fd8035a02e7a64c133f86e3efa9e1b331715648fcab48ba5500`.
- SMBIOS template1942байта: BIOS N.1.05MRO10, SHA
  `0c130254055b8d33f6d033db3640d7c3468a004f6a4c051fa5a118e6c40aef07`.
  Generic boardGM5AR50O/RPL_Refresh остаётся template, не live DMI.

В новом EC:

1. Physical0xDAEB содержит единственный `90 07 40 74 19 F0`:
   запись **0x740=25**, соответствующая сохранённому exact GM6 Project25.
2. Physical0x104EC содержит initializer
   `E4 90 9C CC 93 90 04 54 F0`.
3. В том же bank1 по logical9CCC/physical11CCC находится **4C**.
4. В ранее разобранном BIOS09 SmbiosBoard formatterRVA868 читаетEC0454,
   делит major=`raw>>6` иminor=`raw&3F`; caller924 пишетSMBIOSType0+16/+17.
   Применение этого ABI к4C даёт **1.12**.
5. Сохранённый live dump175302 подтверждает **0454=4C,740=19**, valid mask обоих
   байтов установлен; его inventory сообщаетSMBIOSEC1.12 иBIOS N.1.05MRO09.

Поэтому MRO10 package дал **применимый family/Project25/version1.12 code
candidate**, а не перенос алгоритма из Wujie или только совпадение имени файла.
Сам formatter взят из проанализированного BIOS09; тождество всей BIOS10 codebase
не заявляется. Полная физическая идентичность текущей flash, все board options
и реальное исполнение найденных ветвей требуют отдельного доказательства.

## Связанные новые выводы

EC1.12 current/voltage handoff и глобальные побочные ветви platform7C3 описаны
в [ec-handoff-audit/REPORT.md](../ec-handoff-audit/REPORT.md). Root ведёт Healthy
и adaptive aging, отдельный агент — percentageReached/restart. Старый предел
MRO09/EC1.11 больше не используется как единственное firmware доказательство.

Воспроизведение identity/evidence: `../ec-handoff-audit/collect.py` читает только
файлы. Полные короткие SIG bodies сохранены hex вJSON; аппаратных APIs нет.
