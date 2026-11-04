# Honor HLYL-WXX9: поиск аппаратного Manual на10 ступеней

01.11.2026. Только статический анализ сохранённого BIOS1.09, EC-образа и
OEM-бинарников; дополнительно проверены открытые первичные источники.
Приложения/драйверы/аппаратные пробы не запускались, записей EC/SMU/flash нет.
Исходники управления устройствами и минипк не менялись.

## Подтверждённый результат

Загрузчик пользовательской10-ступенчатой таблицы в RAM EC **не найден** в
разобранном штатном FAN-пути и проверенных интерфейсах. Это не доказательство
отсутствия любого инженерного интерфейса во всём контроллере. Новый аппаратный
setter и writable capability в MagicControl не добавлялись.

Предыдущее выражение «аппаратно10 точек» требовало уточнения: доказаны10 записей
штатной таблицы в памяти кода EC, а не интерфейс загрузки10 пользовательских
точек. Одна6-байтная запись содержит общие пороги вверх/вниз и два16-bit RPM.
Это10 парных ступеней для двух вентиляторов, не20 независимых температурных узлов.

## Нормальная EC-регулировка: точный путь

Свежий сохранённый BIOS:
`../honor-dump-review-20261101/snapshot/bios-20261101-102705-0/bios.bin`.
SHA256 `8fba67e2dc334c35277b1a2163775b38e5def7dbff51ee176f3af22e3e2f5903`.
Первые128KiB совпадают с разобранным IT8987 update image:
`d4622371275d6f409d50a2ab6390e0ca45e1bd585828041bcba385dc7d9ab819`.
Отдельный live EC flash этим исследованием не прочитан.

Псевдокод по проверенным8051 инструкциям (адреса ниже — file offsets):

```text
thermal_tick():
    scratch_descriptor_pointer = CODE:0x6F39       #135CF /14AB9
    if board_hook_4BF4() ==1:                     #4BF4 = MOV R7,#1;RET
        scratch_descriptor_pointer = CODE:0x6FB1  #135F7 /14B3E
    for sensor in0..7:
        descriptor = CODE[0x6FB1 +15*sensor]
        curve = descriptor.Performance if RAM[0x315].bit6 else descriptor.Balanced
        update_sensor_stage(curve,real_sensor_temperature,hysteresis,dwell)
    stage = slew_towards(max(sensor_stages),0..9)
    RAM16[0x534] = CODE16(curve +6*stage +2)        #requested fan0 RPM
    RAM16[0x53A] = CODE16(curve +6*stage +4)        #requested fan1 RPM
    adjust_PWM_from_RPM_feedback()
```

Указатели дескрипторов/кривых имеют C51 memory type `FF`.
Чтение generic-pointer type `FF` идёт через `MOVC`, не `MOVX`:
`7785..77B1`, `7899..78D0`, `7AD6..7B03`.
Generic stores `77B2..77E5` записывают DATA/XDATA, а для code type `FF`
возвращаются без записи. Поэтому обычная I2EC XRAM-запись по числу `0x6A9B`
не является редактированием CODE-таблицы с таким адресом.

RAM `0x6A6/0x6A9` — временные generic-pointer variables, не persistent table
selector. `135C1..135FD` назначает их заново на каждом входе в thermal path.
Запросы534/53A тоже являются выходом расчёта и переписываются штатным кодом.
Подмена этих переменных не установлена как автономный пользовательский Manual.

Точные таблицы/пороги: `../honor-fan-curves-20261101/fan-tables.json`.
Код: `../honor-fan-curves-20261101/fan-table-consumers.asm` и
`build/honor-analysis/ec-original.asm`.

## Проверенные кандидаты

### GFNM/SFNM, SMI0xC1

SSDT-007 направляет040C/040D в DSDT GFNM/SFNM. Они копируют вход в DAIN,
вызывают HSMI (портB0, командаC1) и возвращают DAOU.

Свежий WmiCallbackSmm PE SHA256:
`46c601b500ad672c75be6f72a27c41b35c388145c385942f19187e1fcefd467b`.
Он побайтно совпадает с уже дизассемблированной копией.

Callback16F0 сначала вызывает3158 (группы3/5 и unsupported6), затем ищет
пару group/subID в таблице4180. Счётчик41C0=4. Записи по16 байт:

| Group/subID | Callback RVA |
|---|---:|
|06/01|1CAC|
|06/02|1A5C|
|06/07|1E74|
|06/08|1EE8|

Группы04 и подкоманды0D в этой таблице нет. Ветка173C делает `XOR EAX,EAX;RET`
при отсутствии совпадения, без FAN/table write. Нулевой transport/status
такого SFNM не доказывает изменения режима или загрузки таблицы.
Источник: `build/honor-analysis/wmi.asm:16F0..175E`, callback data свежего PE.

### AMD CBS Fan Control / Fan Table Control

Новые свежие извлечения и Ghidra-decompile находятся рядом:
`CbsBaseDxeRN-decompiled`, `SmuV12Dxe-decompiled`. Проанализированы68 функций CBS
и270 найденных функций SMU; счётчики/таймауты записаны в ghidra.log.
Оригинальные PE не исполнялись.

IFR AmdSetup F1..FE содержит Manual Fan Control, Force PWM, Fan Table Control,
Low/Medium/High/Critical Temperature, Low/Medium/High PWM, hysteresis/frequency/
polarity. Это три PWM-узла плюс critical, а не EC-таблица10 парных RPM-ступеней.

SmuV12Dxe `2FA0` читает PCD `4F033..4F03D` в PPTable148h, поля60h..6Ah;
`10B8` называет эти поля Fan Policy/FanTable_*; entry`A18` передаёт PPTable
через serviceD/10, wrapper2248 и MP1 transport79F8:
message3B10528, response3B10564, arguments3B10998..3B109AC.
В этом же модуле есть ForceFanPwm fields7B/7C.
Поддержка управления нашими двумя EC PWM-каналами этим SMU-путём не доказана.

Найдено также сохранение адреса PPTable в MMIO FEDD82BC..BF (функция88E8).
DSDT подтверждает shared WRAM FEDD8200. Прямых `MOV DPTR,#02BC..02BF` в
сохранённом линейном EC disassembly не найдено; это только узкая проверка
непосредственных ссылок. Она не доказывает, что EC никак не читает этот адрес.
По одному сохранению указателя вывод о загрузчике своих10 FAN-точек не делается.

### STMT: реальные пороги, но не таблица FAN

Найденная через Huawei-WMI issue96 команда присутствует и в нашем DSDT.
WMAA02/03 -> STMT. Для CPU она записывает UCPU/DCPU (RAM3D0/3D1), сбрасывает
CPUF/CPDF/TFUC/TFDC; другие ветки задают такие же пары для ряда OEM sensors.
EC13C0E/13C2D обрабатывает эти alarm-пороги, а DSDT_Q40/_Q41 выдаёт WMI0100.
В разобранном FAN-read path пороги10 ступеней по-прежнему читаются из CODE.
STMT не содержит десяти записей или RPM двух вентиляторов. Его влияние на
полную thermal/power policy не измерялось; setter не запускался.

### Fixed RPM / fixed PWM

Native EC command handlers1AE51..1AEE5 переключают042C.bit2 и записывают
по одному байту в053E/053F. Helper1B3C4 — одно-byte store параметра3C.
Ветка13A9A и13922..139C5 использует этот режим для фиксированной скорости.
Точный безопасный диапазон/поведение новогоRPM setter не проверялись; transport
allowlist MagicControl не расширялась. Это не загрузка60-байтной кривой.
Прежний подтверждённый fixed PWM остаётся042C.bit1 +0532/053C.

## Интернет: что действительно нашли владельцы

Открывались поиски RU/EN/ZH, первичные репозитории, issues и comments. Ни один
из нижеперечисленных материалов не подтверждает автономную загрузку10 точек
в EC нашего HLYL-WXX9/4600H. Отсутствие найденного материала не означает,
что такой способ нигде не существует.

| Источник | Проверенный предмет | Значение для HLYL |
|---|---|---|
|[4PDA,2020–2025](https://4pda.to/forum/index.php?showtopic=998908&st=1960)|Владельцы MagicBook Pro2020 обсуждают отсутствие FAN controls, BIOS/power обходы|Нет показанного loader10 EC points; часть страниц доступна только через индекс поиска|
|[NBFC1325](https://github.com/hirschmann/nbfc/issues/1325)|Honor MagicBook15: DSDT разобран, прямые EC writes автору не помогли|Comments содержат только stale-bot, рабочего config там не найдено|
|[NBFC1285](https://github.com/hirschmann/nbfc/issues/1285), [1287](https://github.com/hirschmann/nbfc/issues/1287)|D14 AMD2020 / MateBook16 AMD5800H просят поддержку|В проверенных comments только stale-bot; это запросы, не реализации|
|[MateFan](https://github.com/shockwave-coder/matefan), [discovery](https://github.com/shockwave-coder/matefan/blob/main/docs/discovery.md)|FLMH-XX2024/CoreUltra155H; editor2–10 points, HWMI0211|CurveController рассчитывает RPM в приложении; FanService timer2s вызывает SetFanSpeed. Не автономная EC table|
|[matebook-fanctld](https://github.com/duny31030/matebook-fanctld), [разбор](https://github.com/duny31030/matebook-fanctld/blob/main/docs/ec-reverse-engineering.md)|KLVG-XX/M1010: SFND даёт фиксированный RPM, Python daemon реализует curve|Наличие SFND на той плате не переносится на нашу: в HLYL SFND нет|
|[NBFC-linux256](https://github.com/nbfc-linux/nbfc-linux/issues/256)|D16 MCLG-XX2024, handshake0618 +set0211, сохраняется последний RPM|Persistence фиксированной скорости не означает persistence curve; в нашем dispatcher этих FAN команд нет|
|[FRI-H76 Ryzen7840HS](https://github.com/sealeelike/Honor-FRI-H76-fan-controller-for-linux)|Автор удерживает режим/скорость циклом0.5s|Это host control, не загрузка таблицы; чужие регистры не использовались|
|[Honor DRA-XX2024](https://github.com/Little-King2022/honor-fan)|Другой extended mailbox/manual selector|Автор сообщает о проблемах повторной установки скорости; не основание writable admission на HLYL|
|[Huawei-WMI96](https://github.com/aymanbagabas/Huawei-WMI/issues/96)|Предложен STMT для thermal thresholds|Кандидат проверен в нашем DSDT; см. выше — alarm pairs, не10 FAN points|

Одинаковый Ryzen не устанавливает совместимость EC, ACPI methods, mailbox или
таблиц. Нашему BIOS не приписываются методы/регистры более новых Intel/AMD моделей.

## Граница реализации

Формат штатных10 ступеней известен. Для аппаратного пользовательского Manual
через ядро всё ещё нужен подтверждённый runtime loader или persistent RAM
source для EC thermal routine с ownership/readback/rollback. Их в проверенном
пути нет. Модификация CODE-таблиц в образе — уже изменение прошивки, а не обычный
RAM setter; допустимость flash-операции, целостность и recovery этим отчётом
не подтверждены. Бинарный patch/flash-команда не создавались и не запускались.

Host-side10-point loop можно построить на существующем fixed PWM, но он требует
работающей программы. Такой loop не выдан за запрошенный автономный аппаратный
Manual и не добавлен отдельным GUI/worker обходом архитектуры.
