# MINISFORUM EliteMini HPBSD

## Текущий контракт,01.11.2026

Продолжение01.11: пользовательская модель — общий FanCurveProfiles16/16
с активными8/3 из DeviceProfile. Custom хранит один curves-массив; отдельный
native draft/preset payload удалён. Старые v3 native-блоки импортируются
поточечно, неактивные ячейки остаются; исходный файл сохраняется в legacy-backup.
Общий CSV v4 хранит активные размеры и подтверждённый режим. Legacy
hpbsd-fan-curves.txt остаётся только источником чтения при первом импорте;
новые записи идут в hpbsd-fan-profiles.csv тем же механизмом, что остальные.
Общий ConfigurationRestore перенесён в platform/windows и восстанавливает
Manual/Min/Max/Default и CPU на startup/power_resume через ресурсные входы.
Mode intent сохраняется после readback, внутренний exit/recovery его не стирает.
У старых файлов без записи выбранного режима автоматическое включение Manual
не угадывается; первый явный выбор создаёт restore intent. Сохранённые точки
сохраняются и до этого выбора. Нижний протокол, ownership, критические ступени,
readback и rollback не заменяются. Ниже — исторические этапы и их границы.

Это17-е устройство каталога, mini-PC, не Uniwill ноутбук. Windows profile
разрешает FANs, SMU telemetry и два штатных CPU-профиля. Батарея и подсветка
объявлены физически отсутствующими; общая модель карточек скрывает их на обеих ОС.
Windows показывает все8CPU/3System точки в Default и Manual. Последняя MAX-точка
видна, но остаётся read-only: её скрытие в прежнем Manual не меняло саму кривую.
Ниже сохранены этапы подключения, включая ранний telemetry-only этап.
После0.1.237 прежние кривые восстановлены: менять можно границы редактора
20–100°C/5–100%, без пересэмплирования, смены числа точек или исполнителя.
Старые описания промежуточной миграции/нормализации не являются разрешением
повторять их. Фактические таблицы и guards — в текущем controller/protocol.
AMI AFU read: два32МиБ ROM SHA c7c0247ae493ccae23cc790de9acc4f1bbd905953dbe688b08caa4af4bc4a379.
Linux не получает Windows I2EC writer по совпадению интерфейса.

### Balance / Performance через общий ресурс мощности

executePowerResource(Stock) обслуживает GUI, CLI и startup/resume. Typed
HpbsdPowerControl допускает только exact HPBSD/AMI1.06/AMD19h:75h и свежий
PM4C0009, внешнее питание и EC361=0C. Лимиты STAPM/Slow/Fast взяты из
FchTacomaSmmInit текущего owner BIOS: Balance54.4/60/65W,
Performance70.8/70.8/70.8W. MP1 подтверждает значения повторными чтениями PM.
Неподтверждённое применение откатывается к исходной тройке. Штатная команда
допускает только точные BIOS-тройки; произвольные значения идут через общий Custom.

При Default CPU/System FAN-таблицы переходят на заводские B1/B2 через прежний
prefix planner и readback. Сначала усиливается охлаждение при повышении мощности,
сначала снижается мощность при возврате Balance. Selector32F фиксируется последним.
Leasev5 сохраняет оба канонических конца и поддерживает восстановление после
прерывания; старые v2–v4 читаются. Активные Manual/Min/Max не переписываются:
все47байт FAN-policy должны совпасть до/после CPU-команды. При последующем
Default используется таблица выбранного штатного режима. Сохранённый Manual
draft одинаков для B1/B2, без пересэмплирования или замены пользовательских точек.

Выбор в settings/hpbsd-power-mode.ini сохраняется только после подтверждения.
Отсутствие выбора не вызывает аппаратного replay; ошибка файла не заменяется
догадкой. Restore — конечная отменяемая работа общего executor, тот же Stock
dispatcher, без GUI writer. Suspended/closing отменяет ожидающую работу;
более поздняя явная команда отменяет старый replay.

BIOS Setup/CMOS/SMI не записываются. Это применение штатных значений и FAN-таблиц
на работающем Windows, не переключение сохранённого BIOS Setup. Старые пробы
MP1 с20/25/30 и8/10/12W подтверждают transport1:1mW; физическая проверка новой
кнопки Performance остаётся за владельцем. Custom1/2/3 — наши пресеты, не три
BIOS-режима.

### Общий Custom, продолжение01.11.2026

В0.1.430 Custom оставался ошибочно stock-only. HPBSD теперь использует тот же
executePowerResource(Custom/CustomEditor/Preset), редактор в два нажатия,
CustomPresetLibrary и selected/active, что остальные модели. Диапазон ЗАПРОСА
общий5..255W (kApuManualMaximum), не старая граница диагностической утилиты65W
и не утверждение о достижимой мощности. Exact identity/PM4C0009, внешний БП,
STAPM<=Slow<=Fast, MP1 ACK, повторный SMU readback и откат исходных mW обязательны.
Нельзя включать запись лишь снятием GUI-disabled или переносить компенсацию Honor.

Редактор/CLI Custom меняет только CPU, проверяя неизменность47 FAN-байт.
Пресеты1/2/3 применяют свои мощности и native Manual через общий FAN entry.
Native8/3 точки хранятся необязательным блоком v3 той же библиотеки пресетов:
старые v1/v2 читаются, остальные устройства продолжают записывать v2. Legacy
16-point CSV не конвертируется: при первом использовании native slot берёт
текущий собственный Manual draft. Без запроса владельца кривые не пересэмплировать.
CPU failure после FAN-шага сообщает частичный результат; preset selection не
сохраняется как успешный. Это прежний порядок общего Custom, не новая атомарность.

hpbsd-power-mode.ini v2 хранит Stock/Custom и native base, читает прежний v1.
Startup/resume восстанавливает CPU через тот же resource entry; FAN replay
к CPU-restore не добавлен. Исходные legacy curve payload остаются в библиотеке.
Пустые app-пресеты используют20/25/30,40/45/54,54/60/65W как Quiet/Balanced/
Performance приложения. Это не дополнительные BIOS-режимы или физические тесты.

Актуальный BIOS: Users_logs/MINISFORUM_HPBSD/20261101-124717-MSK/snapshot/.
Две32MiB копии совпали SHA75220b7b7f684a27b3d1241a4dcb4a71800ab5b1722176050f2f8faf4d4d3103.
FchTacomaSmmInit и встроенный EC-кандидат совпали с прежним разобранным кодом.

## Unified Manual fields, 27.10.2026

Owner's final editor rule: temperatures20–100C, nominal RPM percentages5–100.
Manual exposes7CPU/2System ordinary points, all editable. As of01.11 mandatory
MAX is also visible as the final read-only row. Hot bases51/40, upper100,
slopes0 and independent CPU>93C override remain immutable. Last ordinary
upper and hot lower move together, never later than original93/83C. Requested
20..100C points stay unchanged in settings/fields; encoded boundaries above
the independent MAX guard are projected earlier. The graph uses actual EC
projection, never silently presents the requested100C as the hardware guard.
Planner seeds normal rows toMAX before
any boundary move. Default restores all34mutable fields exactly. Leasev4
admits exact legacyv2/v3 endpoints; old curve draftv1 migrates in memory to
20–100C with2C spacing, saved explicitly asv2. No automatic hardware replay.

Min preset stays at factory36/38%; it does not restrict the editor to those
values. Physical RPM, nominal target percent and measured PWM stay distinct.
The following older description of locked ordinary boundaries is superseded.

## Settings paths, follow-up27.10.2026

All threeHPBSD files (fan ownership, old fixed targets and curve draft) are
registered in PortableSettings. The existing checked migration imports old
root files into settings/ and retains legacy-backup; caller file formats and
receipt semantics are unchanged. New controller writes stay in settings/.

## Native curve editor and PWM,27.10.2026

Manual now uses the existing point fields/graph with native8CPU/3System bands.
Upper temperature boundaries and nominal RPM percentages are model data; lower
boundaries/slopes are encoded by HpbsdFans. CriticalCPU94+/System83+ rows and
their triggering boundaries remain immutable. Normal temperature changes seed
normal bases toMAX first, so every interrupted apply/restore prefix is bounded.
Leasev3 includes30normal policy fields and curve kind; v2fixed leases migrate
with canonical temperatures. Default restores the exact selectedB1/B2 table.
The same controller persists validated drafts tosettings/hpbsd-fan-curves.txt;
editing/saving a draft does not write EC, and there is no startup auto replay.
Fixed-target API remains available forMin/Max and oldCLI consumers.

Default plot comes from observed bases/slopes, CPU target=floor(deltaT*slope/100)
plusbase. System uses discrete bands. The marker uses EC control temperatures
0888/0889 and physicalRPM normalised to nominal maxima; it is not a PWM point.
PWM telemetry separately reads1803/1804 with checked1841=127. NoPWM/SFR writes.

Hardware probe curve-cycle changed ordinary boundaries27/30C and targets,
observed targetcodes24/22 andRPM~2460/~2140, then restored every factory field.
4598offline interrupted-restore prefixes preserve hot rows/override fields.
Above-hot thermal transitions and liveB2 remain untested; firmware tracing and
offlineB1/B2 validation establish the protected layout. Earlier fixed-only
Manual and no-PWM descriptions below are superseded by this narrow adapter.

## DDR5 and explicit SMU validation,27.10.2026

The existing PawnTelemetryReader now admits SPD5118 page recovery only for
exact HPBSD/BIOS1.06/CPUID19h:75h, current AMD SMBus port0, addresses50/51.
Observed MR11=7 hides volatile ID/temp registers. The pure Jedec helper checks
zero type/vendor and page7, temporarily selects page0, validates SPD5118,
reads MR49/50 and restores page7 with readback even after a failed transfer.
The existing shared SMBus mutex covers the entire transaction. No EEPROM,
MR26, alarm, threshold, frequency or mux payload is written. Unconfirmed
restoration poisons only that reader. Other models keep the original getter.
Normal repeated successful page accesses are not appended to the log each poll;
first access and explicit audited diagnostics retain the transport evidence.
Live memory temperatures around37.5–38.25/35.75–36.25C reached the shared sensor
snapshot during both CPU/SMU validation runs; no guessed physical slot pairing.

An explicit ApuPowerReader HPBSD method shares the MP1 transport with Honor,
but has its own exact board/table bounds and response register3B10578.
Honor retains3B10564, its admission/ranges and coefficient policy unchanged.
HPBSD permits5–65W, ordered STAPM<=Slow<=Fast, exact1:1 milliwatts only,
shared PCI mutex, repeated fresh PM4C0009 confirmation and checked rollback.
Both20/25/30W and8/10/12W were physically confirmed, then54.4/60/65W restored.
At Slow10W the same CPU workload settled around10W instead of14.8W, verified
independently in SMU and package RAPL. The short run does not validate long
STAPM time constants. All47fan-policy bytes remained identical.
Only the internal explicit probe invokes this setter: no new SETs presets,
startup/sleep replay or power-control capability has been enabled.

Evidence: research/minipc-hpbsd-20261026/power-memory/verify-power-memory-20261027.
Earlier read-only descriptions below are superseded for this explicit probe
and the narrowly admitted temporary SPD selector operation only.

## Fan control and full RAM capture,26.10.2026

HpbsdFanController is now the shared GUI/CLI writer, via NativeFixedFans.
Default/Manual/Min/Max use the exact own canonical B1/B2 policy and journalled
normal-row writes. Manual percentages are nominal RPM target scaling, not PWM.
Minimum nonzero factory targets1800/1500RPM and maximum target codes51/40
are enforced. CPU critical row and system83+C row remain OEM-owned throughout;
system FF override is never enabled. Typical live Max~5074/~3535RPM, Manual
65/65~3333/~2580RPM and Min~1790/~1500RPM; Default readback verified at the end.

Shared typed I2EcWindows session owns the ISA mutex, physical identity, narrow
writer allowlist and checked parking. No raw write API is exposed. Owner lease,
readback, atomic prefix journal, bounded recovery, thermal watchdog and exit
cleanup belong to this controller, not UI buttons. Read-only observers do not
apply saved state. A recognised interrupted transaction requires explicit
Default; unknown foreign tables remain untouched. Manual draft targets persist
in settings/hpbsd-fan-targets.txt. Selected manual mode is not auto-replayed
after firmware resets, sleep or process restart; the UI reports fresh state.

Explicit EC dump captures every byte of0000–0FFF using bounded16byte sessions
and cancellation. No SFR/flash sweep; fixed hardware tach getters stay separate.
fan-tables-default.json contains actual CPU/system RAM rows and slopes, not an
invented firmware-flash export. Dump never selects Default/Max, even when the
collector's generic fan-probe option was requested. Linux writer remains absent.

The earlier section below documents0.1.221 read-only admission; its exclusions
of fan writes/raw diagnostic RAM are superseded by this narrower reviewed path.

Exact SMBIOS: `Micro Computer (HK) Tech Limited` / `HPBSD` / `EliteMini Series`.
The owner's unit has Ryzen7 8845HS, AMI BIOS1.06, SMBIOS EC0.2.
The UI uses the board name: the SMBIOS string alone does not select a marketing
model. Official UM880Pro/UM890Pro support: https://www.minisforum.com/pages/product-info

## Confirmed read-only integration,26.10.2026

Windows admission requires the exact SMBIOS tuple, BIOS1.06 and AMD CPUID
family19h/model75h. CPU Tctl/core energy/package MSR and SPD use the existing
signed, pinned PawnIO modules. ProcessorInformation frequency uses the same
getter as Intel. No CPU-power setter is exposed for this board.

`TelemetryWindows.cpp` reuses PawnLpcIo with the global ISA mutex. Its entire
XRAM interface consists of fixed getters; I/O writes select and park address
latches only. PNP5571 and live XRAM2000/2001/2002 must agree, and live controller
bytes200D/180C/1841 must be43/14/7F. A mismatch refuses telemetry. Abandoned
mutex/failed parking poison that reader. No PNPCFG entry/exit sequence from
Honor is borrowed.

CPU/system tach counters181E–181F /1820–1821 use bounded low/high/low reads;
RPM=2156250/counter. Zero counter gives zero RPM. EC temperatures0309/0305
are CPU/system, respectively. System temperature and system-fan RPM are never
labelled as GPU measurements. No PWM value is invented from requested RPM.

SMU uses the existing ApuPowerReader with exact CPUID/host and PM4C0006–4C0009
schema admission; the live unit returned4C0009 and54.4/60/65W STAPM/Slow/Fast.
This is limit telemetry, not proof of load enforcement and not a setter.
At the initial telemetry-only stage capabilities granted SmuTelemetry; fan/charge/lighting/power-control
flags and raw EcAccess transport remain absent. No startup settings replay,
factory mailbox, Uniwill register map or ROM-ID procedure is enabled.

Linux recognises the board and shares GPU presentation/UI. Its existing system
monitoring remains available, but no Windows I2EC or PawnIO port is claimed.

## Evidence

Owner data: research/minipc-hpbsd-20261026/followup/ (120 hash-checked files).
BIOS ROM AFU32MiB two-pass SHA c7c0247ae493ccae23cc790de9acc4f1bbd905953dbe688b08caa4af4bc4a379.
Embedded EC candidate first128KiB SHA15fb6994c9c5292df51441cf49b876070785f95314efe02a03fea3c55065a994,
bannerCMXG_EC-V14.6. Candidate is not a live EC-flash read.
Firmware analysis records the B1/B2 CPU-policy branches at9DF7/9E3B, native
CPU critical row at8325 and separate system policy. These facts alone do not
enable a safe manual/automatic fan writer; ownership, restoration and physical
fan response need their own implementation and validation.

Protocol facts were compared with zuyan9/FanControl.MinisforumUM commit
ec941c0f17b74e5773125fda0f40b621e14f119e, especially docs/technical.md,
PlatformProfiles.cs and PawnIoNative.cs. No source or plugin binary is shipped.
CPU PM offsets/version list: FlyGoat/RyzenAdj lib/api.c (read-only fields).

Live getter checks: CPU37.25–37.875C; CPU fan1960–1964RPM; system fan1516–1525RPM;
EC CPU37C/system34C. CPU core0.45–1.13W and package4.3–5.4W observed independently.
SPD5118 getters at50/51 returned no temperature; do not assert hardware absence.

## Shared GPU correction

RDP exposes two LUIDs for one present Radeon PCI devnode. Provider identities
may join aliases only when the independently verified physicalId agrees; raw
OS/provider evidence remains separate. Presentation collapses aliases and
normalises display routes. Different or absent physical IDs never merge by
name. Microsoft1414:008C is a software renderer even when a duplicate handle
omits the DXGI SOFTWARE flag. Missing legacy ADL readings cannot overwrite fresh
bound ADLX temperature/load/clock/power.
