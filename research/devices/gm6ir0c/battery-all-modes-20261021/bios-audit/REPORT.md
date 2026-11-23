# MRO09 BIOS/SMM и идентичность EC: статический аудит

22.10.2026, продолжение исследования21.10. Только сохранённый скачанный
контейнер/распакованные файлы. CAP/BIOS/EC/модули не исполнялись; устройства,
EC/UEFI, WMI/AML methods не вызывались. Ghidra/dumpbin/iasl использовались
только как анализаторы файлов. Production/test/build продукта не менялись.

## Главный результат

**Принадлежность BIOS к той же MRO09 firmware family подтверждена значительно
сильнее имени файла. Но вложенный EC кандидат инициализирует версию1.11,
тогда как сохранённый ноутбук сообщает1.12. Это не exact-byte image текущего EC.**

Старый вывод «исполняемый EC код не получен» теперь устарел применительно к
новому пакету: root извлёк8051 candidate и исследует его consumer отдельно.
Прежние отрицательные результаты exact OEM host-аудита остаются результатами
анализа того EXE; новые firmware выводы имеют отдельно указанную границу версии.

## 1. Исходники и проверка состава

Использован только **полный flat dump** `build/g9.bin.dump`, не первый
частичный MAX_PATH dump. Исходный raw BIOS:

`build/gm6-battery-firmware-20261021/BIOS-MRO09-from-UCP.bin`

- 24117248байт (`0x1700000`).
- SHA-256 `e6d9205000bfa888e48f4499f620f7d9725334827dbbb2e750388bb4b51ff688`.

Просмотрены341 PE32 body. Их manifest — [pe-manifest.json](pe-manifest.json).
Byte-pattern поиски MMIO/offset/GUID/строк дают124 модуля, но **сырые совпадения
не считаются xrefs**: FE410000 может случайно встретиться внутри другой
инструкции/числа. Ghidra дополнительно проанализировал9 выбранных OEM/SMBIOS
модулей, экспортировал316 функций без decompiler-error files. Полные результаты
лежат в `build/gm6-bios-static-export/`; SHA каждого input/output —
[decompiler-manifest.json](decompiler-manifest.json). Существенные функции
с номерами строк сохранены в [decompiled-evidence.json](decompiled-evidence.json).

## 2. Сопоставление DSDT — точные байты методов совпали

Из BIOS извлечён `Section_Raw_C118F50D-391D-45F4-B3D3-11BC931AA56D_body.bin`:

- 668509байт, checksum корректен.
- SHA-256 `3afe43f1f04184e3ca855d8162e0283af0f677b05465c8137d6109d4b301f00a`.
- OEM ID ALASKA, OEM Table ID `A M I`.

Два независимых сохранённых DSDT этого ноутбука имеют ту же длину:

|Сохранённый источник|SHA-256|Отличий от BIOS template|
|---|---|---:|
|samples/DSDT.aml|2a4d766944f5416e275f0effbf478b90bdfa1e83d2875ae1aee11caac4bad4d1|84байта в37 диапазонах|
|dump175302 saved-acpi|1c2b99426121c578e0e61b45a700731e7fb044abc0fce4f45a262a562e99d737|84байта в37 диапазонах|

Большинство различий — ожидаемые template placeholders FFFFFFFF/FFFF0000/55AA,
заменённые RAM-адресами/длинами NVS/TPM/GNVS/Uniwill regions, платформенные
Name literals/PCI_ADR иchecksum. Они перечислены **все** в inventory.json,
не вырезаны перед вычислением исходных SHA. Это не побайтная идентичность всего
DSDT и не заявление, что все patches выполнены одной и той же DXE функцией.

Отдельно извлечены AML MethodOp packages с проверкой package length. Следующие
методы совпали **полностью, на тех же file offsets**, в BIOS template и обоих
сохранённых DSDT:

|Метод|Offset|Байт|SHA-256|
|---|---:|---:|---|
|MMRW|647586|184|4e7befd4c0939539a55f3ff4ff8988bc3761dd077026e4b3834001d33bdbf11e|
|ECRW|647867|23|4935d379629837683607b6af825928d5455521cba15151f7a3a9f22e7c7dea99|
|_Q53|666192|56|939a2da06fc96864750365658bb91f755f3c910cd0cfd7f2b4b71cb1d5f308c2|
|AC0._PSR|666957|106|98e69c8d6a80fa56ce69513aa8604554ab577c057e05bb9cefb337d54fb14e6c|
|BAT0._BIF|667099|461|fd68dd767f9e3dab4fe2ae01fe1a81068e36e825d02ec66b0e63e07539719795|
|BAT0._BST|667572|453|447265f194d20a744bc216573c7a6e0c3b2e64efd451784310cbf078b7c647bc|

Полные hex/method hashes — [identity-evidence.json](identity-evidence.json).
Новый AML дополнительно разобран iasl20251212, exit0,
`../Decompile/GM6IR0C/BIOS_DSDT/firmware.dsl`. AML не выполнялся. Следовательно известные
ISDB/cycle capacity remap и CGLM→BatteryState bit3 подтверждаются здесь теми же
байтами, а не только похожим названием платы.

## 3. Family identity: ещё два независимых совпадения и DMI caveat

1. `$FID` freeform GUID2EBE0275... содержит `GMxIR7xN1`; отдельный `$SGN$` body
   содержит `BIOS Date:11/28/2024 16:22:32 Ver:N.1.05 MRO09`.
2. ESRT family GUID **13EEDDA5-5552-5DF0-A651-569AA1396B84** найден в формате
   little endian внутри FmpDxe/FmpCapsuleUpdate/CapsuleRuntimeDxe/ReFlash.
   Тот же GUID и BIOS N.1.05MRO09 находятся в сохранённом exact inventory
   `dump-20261021-175302-0/inventory.json` (selected rows+sourceSHA в evidence).
3. DSDT battery/transport/event method bytes совпадают, как показано выше.

**Caveat:** SMBIOS raw template GUIDAB56DC60... содержит
`Jiguang15 Series GM5AR50O`, boardGM5AR50O иRPL_Refresh, неGM6IR0C. Это не повод
объявить свой ноутбук GM5AR50O: в firmware есть DMI/runtime patch machinery,
template не является снятым current SMBIOS. Но одним этим template нельзя
доказать exact GM6 или пригодность прошивки. EC version bytes в Type0 template
offset0x16/0x17 — **FF/FF** до runtime заполнения.

В итоге доказана согласованная family/BIOS basis. Не доказаны полное совпадение
SKU feature configuration, безопасность прошивки или идентичность live EC flash.
Никакого разрешения/попытки flash из этих совпадений не следует.

## 4. Сильная граница: кандидат EC1.11 против live1.12

EC section `207F94A8-238E-11E8-B467-0ED5F89F718B`:
131072байта, SHA-256
`5059e6cb58addec21ad7fd5693f4c082230fbd8c7692fb07799c90adabae5c4c`.
Root отдельно восстановил8051/bank architecture иProject0x740=25.

Здесь независимо найден **BIOS formatter**, связывающий EC байт с SMBIOS:

- SmbiosBoard GUIDCEF68C66..., function RVA0x868 вызывает команды EC
  A3=4, A2=0x54, A4/read через порты66/62: адрес **0x454**.
- Возвращённый byte разбивается `major=byte>>6`, `minor=byte&0x3F`.
- Caller RVA0x924 записывает результат именно вSMBIOSType0 offsets
  **0x16/0x17**, то есть Embedded Controller Firmware Major/Minor.

В кандидатном8051 image единственная последовательность
`E4 90 9C CC 93 90 04 54 F0` находится на физoffset **0x104EC**:
`A=0; DPTR=9CCC; MOVC A,@A+DPTR; DPTR=0454; MOVX @DPTR,A`.
Сама эта функция исполняется в выбранном bank1 window по логическому84EC;
между MOVC setup и MOVC нет bank switch. По подтверждённой root-модели
fixed0..7FFF + selected bank slice, logical9CCC этого bank →physical11CCC.
Там **0x4B**: `4B>>6=1`, `4B&3F=11`.

То есть нормальная initializer/SMBIOS цепочка сообщает **1.11**, в то время
как сохранённый ноутбук сообщает **1.12**. Строка `ITE EC-V14.6` в заголовке
относится к SDK/core и **не является версией прошивки устройства**.

Выводы8051 consumer из этого кандидата следует маркировать **MRO09 bundled
EC1.11**, пока не найдён/сопоставлен исполняемый1.12. Возможные более поздние
динамические записи/патчи также нужно исключать отдельно; live flash dump здесь
не получен. Это сильнее семейного имени, но не заменяет побайтное сравнение1.12.

## 5. BIOS/SMM battery consumer: чего в выбранных9 модулях не найдено

Статически разобраны:

- OemServiceSmm7C2D1E53... —149 функций;
- OemACPIDriverSmmD780600B... —18;
- OemHooksSmmDE6DB1B8... —14;
- OemUniWillVariableDxe0B2B4F68... —12;
- OemPowerModeDxeCE159F09... —11;
- OemHooks1CEE181A... —36;
- SmbiosBoardCEF68C66... —44;
- SmbiosMiscD22C15F5... —20;
- SmbiosGetFlashDataDED7956D... —12.

В этих selected decompilations не найден прямой конкретный battery
`FE4107A6/7B9/7D0` consumer с алгоритмом CV/CC/stop/restart. Это **ограниченный
отрицательный результат**, не полная анатомия всех341 PE и не отрицание EC consumer.
Generic pointers/indirect protocol calls и нераспознанные Ghidra function entries
могут потребовать дополнительного анализа. Например callbackRVA1550 в
OemACPIDriverSmm показан какLAB; инструкции просмотрены отдельно dumpbin:
он проверяет protocol-state byte и выставляетstatus3, не конкретные charge regs.

Полезные конкретные границы:

### UniWillVariable initial layout

OemUniWillVariableDxe RVA528 читает180-байтную UniWillVariable. Если отсутствует,
создаёт default prefix, читает ECProject740 иrelated DMI fields. На смещениях
**2F/30/31/32** initial DWORD=FFFFFFFF: PowerMode/BatteryLimitation/Maximum/
Minimum остаютсяFF. Это согласуется со снятой180-байтной переменной, а не является
скрытым применением Healthy. Этот фрагмент не превращает FF в charge stop command.

### PowerModeDxe — другой смысл слова power

RVA568 используетPowerMode(offset2F), 751,727.6,782.4 и49F.1. Он восстанавливает
Office/Gaming/Turbo/Custom power indication/configuration; это **не**
BatteryProtection2 mask7A6 и не percentage API. Не связывать boot native
performance defaults со включением ограничения батареи по одному слову Power.

### OemServiceSmm ROMID functions — не найденный charge gate

RVA62F4/6430 читают/устанавливают **14 bytes770..77D**, затем setter ставит
77E=A5/77F=78. Строки прямо называютGet_ROMID/Set_ROMID. Loop ограничен14,
не проходит до7A6. Это опасный для переноса generic vendor service command,
но в этих функциях нет найденной связки сHealthy/7B9/7D0. Его не вызывали.
RVA6728 принимает произвольный offset дляgeneric EC/MMIO helper; само наличие
доступа не является конкретным charging algorithm.

## 6. Ограничения и связь с root EC-аудитом

Новый пакет даёт настоящий исследуемый firmware candidate и сильное family
сопоставление. Он **не доказывает**, что частный цикл изEC1.11 совпадает с1.12
на живом ноутбуке. Отсутствие конкретного consumer в выбранных BIOS PE не
перекрывает найденный root8051 consumer. Его ветви APP741.0/7A6 и numeric scales
сохраняются в отдельном ec-audit отчёте, здесь не переинтерпретируются.

Не выполнялись writes, чтение live flash, обновляющие OEM компоненты, AML
evaluation, аппаратные тесты или сборки MagicControl. Не менялись ранее
сохранённые выводы OEMаудита задним числом; новая версия доказательств находится
в этой отдельной папке.

## Воспроизведение

`inventory.py` читает flat dump и два savedDSDT; `identity.py` проверяет selected
AML method bytes, signature identity, live-inventory selected rows иECversion
инициализатор, сохраняет хэши/выдержки. `ExportGm6Bios.java` — Ghidra post-script
только статического экспорта. Логи parser passes:
`research/build-evidence-20261107/build-root-logs/gm6-bios-service-ghidra.log`,
`research/build-evidence-20261107/build-root-logs/gm6-bios-other-ghidra.log`,
`../Decompile/GM6IR0C/BIOS_DSDT/iasl-output.txt`. Java/Ghidra user directories перенесены
в ignoredbuild, глобальные настройки и бинарные модули пользователя не менялись.
