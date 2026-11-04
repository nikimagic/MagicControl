# GM6IR0C — независимый аудит всех OEM battery host modes

21.10.2026. **Только сохранённые файлы.** Приложение, OEM EXE/DLL, драйверы,
EC/UEFI/SMBus/ACPI methods не запускались. Производственный код, тесты, сборки,
portable и личные настройки не изменялись. Существующий Decompile.dll работал
как PE/CLI metadata parser, не загружал исследуемую сборку для исполнения.

Прочитаны прежние `battery-healthy-20261021/README.md` и
`percentage-search-20261021/REPORT.txt`. Их результаты не выдаются за новые.
Ниже отдельно приведены **новая бинарная проверка**, дополнительные разобранные
ветки и сохраняющаяся граница firmware consumer.

## 1. Что добавлено в этом аудите

Это больше не только `rg` по декомпиляции:

- Exact `GCUService-active.exe`, SHA-256
  `8fd02bf5f42629998d8ec716c3e6ff906baf73cb356b1fdd7585a26d77a49daf`.
- Свежо прочитаны **20802 metadata token names**, включая настоящие enclosing
  names compiler-generated async types, без исполнения OEM assembly.
- Из PE metadata независимо проверены **6124 MethodDef / 8682 Field**.
- Все **5744 непустых IL method bodies** из сохранённого metadata index сверены
  байт в байт с точными RVA/методными заголовками этого EXE. Остальные380 entries
  не содержат непустого IL, а не молча пропущены как ошибки декодера.
- Все5744 разобраны **по границам инструкций**,0 ошибок:104477 token-reference
  instructions и10828 `ldstr`. Это не поиск похожей последовательности байтов.
- Проверены обычные call/callvirt/newobj, delegates `ldftn`, field/type tokens,
  а также строковые имена Microsoft.CSharp dynamic binders. Контрольный пример:
  QC `UserSet_BatteryChargingLimit_Setting` имеет0 прямых method-token refs,
  но **1 dynamic member-name literal** в `QC+<Receive>d__50.MoveNext`. Такой
  путь действительно обнаруживается; отсутствие MethodDef-call само по себе
  не использовано как абсолютное доказательство отсутствия вызова.

Результаты: [cil-reference-audit.json](cil-reference-audit.json),
[source-excerpts.json](source-excerpts.json), [inputs.json](inputs.json).
Скрипт [audit.py](audit.py), framework opcode descriptors
[cil-opcodes.json](cil-opcodes.json), свежие имена [token-names.json](token-names.json).
Точные пути, SHA и номера строк всех основных источников сохранены в inputs/excerpts.

## 2. Карта режимов: три независимые группы

|Группа|Имена/значения|Host-действие в exact OEM|Что этим не доказано|
|---|---|---|---|
|Native battery policy0|PERFORMANCEDMODE / Performance / FullCapacity / High Capacity|7A6.bits4–5=00|Точный зарядный CV/CC и100% физической ёмкости|
|Native battery policy1|BALANCEDMODE / Balanced, в Companion Label также Optimal|7A6.bits4–5=01|Конкретные95/85/80% и условие остановки|
|Native battery policy2|HEALTHYMODE / Healthy / Stationary|7A6.bits4–5=10|Точный предел напряжения/SOC, удержание, restart|
|Experimental percent|Наши100/95/85 + lower80|Отдельная запись7B9.low7 /7D0.low7|Включение firmware percentage consumer|
|Calibration/Protection stages|Старые7%/100%, STAGE_STATUS|Registry/UI/уведомления, а не регулирование зарядника|Физический discharge/charge state machine|
|CPU power on battery|CompanionSettings.BatteryMode типа PowerModeChoice|Выбор Office/Balanced/Turbo CPU/FAN профиля при смене AC/DC|Это не BatteryMode.FullCapacity/Stationary|

**7A6=01 — Performance, не mode1.** Поле выделяется `(raw & 0x30)>>4`.
Если соседний bit0=1, raw01/11/21 соответствуют Performance/Balanced/Healthy.
Raw09 добавляет bit3, а не Healthy. Резервное поле11 (`raw&30=30`) не имеет
ветки в BatteryProtection2.SetHealthProtectionStatus; не приписывать ему режим.

Источники aliases: `Companion.Core/PowerCommands.cs:25–43`, BatteryMode enum;
UWP PRI `strBatteryPerformance`, `strBatteryBalanced`, `strBatteryHealthy`;
`Companion.Ui.Pages.Power.Strings.resx:40–74`. Ресурсы говорят о полном заряде,
защите от высокого напряжения и наиболее сильном ограничении Stationary,
но **не сообщают численные пороги/вольты/гистерезис**.

## 3. Exact активная цепочка: Healthy действительно доходит только до7A6

`PowerPage.OnModeSelected(BatteryMode.Stationary)` →
`PowerCommands.SetBatteryMode` (`HEALTHYMODE`, topic BatteryProtection/Control) →
`MyControlCenter.App.Recieve` (`02000341...App.cs:355–357`) →
`BatteryProtection2.Instance.Receive` → `BatteryProtection2+<Receive>d__38.MoveNext`
→ registry HealthProtectionStatus=2 → `SetHealthProtectionLow` →
`MyEcCtrl.Read(1958)` / `Write(1958, modifiedByte)`.

Новое бинарное подтверждение:

|Метод|Token|Найденные входящие ссылки|
|---|---|---|
|BatteryProtection2.Receive|0600091B|App.Recieve, callvirt1|
|SetHealthProtectionLow|06000924|SetHealthProtectionStatus + generated Receive.MoveNext,2|
|SetHealthProtectionMiddle|06000923|Те же два пути,2|
|SetHealthProtectionHigh|06000922|Uninstall, Disable, status dispatch, Receive performance/recovery,5|
|EnableByService|06000919|App startup MySystem action,1|
|Resume|0600091A|App+ModernOn.MoveNext,1|

Из `SetHealthProtectionLow` нет скрытого второго вызова процентного setter,
UEFI key, FAN/APU режима, charger mailbox strobe или software loop тока.
Код setter: `020001C6...BatteryProtection2.cs:304–312`: read7A6, bit4=false,
bit5=true, write. High/Middle отличаются только двумя битами.

Прежняя цепочка driver/DSDT ECRW → запись SystemMemory FE4107A6 остаётся
подтверждённой предыдущим исследованием; она **не содержит обнаруженного
алгоритма charger CV/CC/stop/restart**. Отсутствие дополнительного host-вызова
не отрицает существование такого алгоритма внутри EC/BIOS/SMM firmware.

### Важная новая оговорка: OEM отчёт не является даже readback

В Receive сначала обновляются переменная/registry, затем вызывается void setter.
`UpdateStatusToClient` возвращает кешированный `m_HealthProtectionStatus`,
не читает7A6 и не проверяет current. `AcpiCtrl.Read/Write:186–227` ловят и
поглощают исключения, Monitor.TryEnter может не войти; `WriteACPI:376–394`
**игнорирует bool DeviceIoControl**, нет финального EC readback. Проверка
CreateFile только `handle!=IntPtr.Zero` также не исключает INVALID_HANDLE_VALUE.
Поэтому UI/registry mode2 доказывает намерение host, но не запись или физический
Healthy. Это описание файлов OEM, не запуск/проверка этих ошибок на ноутбуке.

## 4. Процентная ветка: независимая проверка отрицательного результата

В exact assembly следующие методы имеют **0 CIL references и0 dynamic binder
member-name literals**, включая async/delegate paths:

|Метод|Token|
|---|---|
|SetBatteryChargingLimit_Up|06000926|
|ReadBatteryChargingLimit_Up|06000927|
|SetBatteryChargingLimit_Down|06000928|
|ReadBatteryChargingLimit_Down|06000929|
|set_m_BatteryChargingLimit_Up|06000913|
|set_m_BatteryChargingLimit_Down|06000915|
|LoadBatteryLimitationDefault|0600091D|

`set_m_BatteryLimitationMode`06000917 имеет ровно1 ссылку из
`LoadBatteryLimitationDefault`; сам loader не вызывается. Getters Up/Down
читаются только этим недостижимым loader. Это цельная невызванная host-ветка,
а не пропущенный timer или command alias.

В enum действительно есть CHARGING_UP_LIMIT/DOWN_LIMIT и TYPE_C_ADAPTOR...
(`BatteryProtection2:22–30`), но Receive:154–190 обрабатывает только три режима,
RECOVERY и Report GET. Наличие enum не создаёт dispatch. Даже пришедшая строка
CHARGING_UP_LIMIT здесь игнорируется. Не найден другой exact App receiver для
того же topic, вызывающий private setters.

Codec: Up100 → low7=0, иначе low7=limit, preserve bit7; Down допускает1..95.
**Private ReadDown содержит отдельную странность:**0→95, иначе возвращает весь
Data без `&7F` (`362–371`), тогда как writer сохраняет bit7. Он тоже не вызывается.
Это ещё одна причина не принимать эти helper bodies за законченную спецификацию
измеренного restart. Default100/95 в полях — не подтверждённые95/85 режимы.

### Невызванные UEFI setters не дают обходной gate

Строки ChargeMaximumLimit/ChargeMinimumLimit/BatteryLimitation появляются в
невызываемых property setters (`BatteryProtection2:87–133`). Свежие CLI refs
показывают, что соответствующие три поля `NVRAM_STRUCT` имеют **только один
`ldflda` каждый — из ToString**, без прямых stfld/stsfld consumers.

`NvramVariable.SetFwVars(string,byte):316–376` не содержит этих трёх case.
Неизвестный key всё равно может пройти в сериализацию кешированной целой
структуры и WriteUefi, но battery field от этого key не изменяется. Нельзя
использовать этот код как доказанный enable и тем более запускать его ради поиска.
Read/Marshal целой структуры могут переносить firmware bytes без direct field
store; аудит не утверждает, что UEFI поля вообще никогда не меняются firmware.

## 5. Старый BatteryProtection: это другая неактивная host-ветка

Новая бинарная проверка всех direct method/field/type references к старому классу
и его настоящим nested async types: **0 внешних ссылок** из остальной exact assembly.
`get_Instance`/`Receive`/`Resume`/`SetDispatcher` имеют0 refs. Его private constructor
создаётся только собственной static initializer; внешний вход в singleton не найден.
Отсутствие прямого вызова `.cctor` само по себе ничего не доказывает — CLR может
запускать её неявно; вывод основан на отсутствии внешнего доступа ко всему классу.
Для этих имён нет dynamic binder string, обычный reflection-поиск по всей
декомпиляции также не дал конструктора/метода BatteryProtection. Внешний неизвестный
reflection/native code или другая отсутствующая сборка этим не исключаются.

Что в нём фактически есть:

- `InitializeHealthSwitch:367–377` читает78E.3 как BatteryProctionSupport,
  но всё равно вызывает восстановление registry policy. Это не percentage gate.
- `SetProtectionHigh/Middle/Low:388–421` снова только RMW7A6.mask30.
- `SetHealthSwitch:384–386` — **пустой**.
- `AutoProtection:467–519` смотрит7/100 и меняет stage/registry/UI. Даже входная
  проверка `if(ProtectionSwitch)return` противоположна ожидаемому имени switch.
- `AutoCorrection:521–611` сопровождает ручной цикл пользователя AC100 → DC≤7 →
  AC100, выдаёт уведомления/finish time. EC/charger setter внутри не вызывается.
- `_Timer_Elapsed:679–681` пустой, `CorrectionTime` ничего не применяет.

Следовательно эти названия не раскрывают механизм автоматической остановки
зарядки и не заменяют недостающий consumer Healthy.

## 6. Таймеры и жизненный цикл не скрывают regulator

Активный `BatteryPercentManger` (`020001B8...:19–27,54–65`) имеет10000мс timer:
читает Windows battery percent и при изменении вызывает LifePercentChange.
Подписанный BatteryProtection2 handler только отправляет процент/время вUI.
Его собственное `_Timer=new Timer()` поле не настроено/не запущено в классе.

- Startup → EnableByService → Init → сохранённый HealthProtectionStatus.
- Resume имеет два host пути: App.ModernOn→Resume→SetHealthProtectionStatus
  и SystemEvents.PowerModes.Resume→Init (повторно читает registry).
- AC/DC StatusChange только обновляет UI. Нет перевключения Healthy по проценту.
- Disable/Uninstall/RECOVERY снимают7A6.mask30 доPerformance. Exit OEM-службы
  вызывает Disable, **но сохранённый registry Healthy не стирается**. Следующий
  старт снова применяет сохранённый выбор.
- CustomizeTarget17 defaultBalanced; exactGM6 CustomizeTarget52 → defaultPerformance.
- GetTypeCAdaptorPrioritySupport (`MyEcCtrl:253–256`) прямо возвращаетfalse;
  TypeC enum/options в BatteryProtection2 не образуют active hardware setter.

Это объясняет сохранённое registry2 при rawPerformance после выхода OEM и не
позволяет считать state persistence доказательством удержания.

## 7. QC/SMBIOS percentage API не выбран на exact GM6

У `MyFanManager_QC` есть60/70/80/90/100, dynamic command
BATTERY_CHARGINGLIMIT_SETTING, setter7B9.low7 и resume/first-time paths.
То есть percent helper в этой сборке **может быть вызываемым в другом классе**;
отрицательный вывод нельзя распространять на всю сборку/все модели.

Однако exact `MyFanCtrl.cs:25–74` выбирает RamFan1p5 на Project25, QC constructor
имеет0 direct refs; fresh names не показывают альтернативной constructor MemberRef.
`DetectQCRefreshFeatures` также имеет0 refs. Его `IsQCRefreshBIOS` вызывается только
им самим. SMBIOS type11/string18 feature bit4 существует в helper, но activation
этого helper в exact service не найдено. Никакого допуска GM6 по этому биту нет.

Вывод: не добавлять GM6 в QC путь по совпадению7B9. Не превращать невызываемый
SMBIOS helper в найденное недостающее условие зарядного ограничения.

## 8. Соседние биты/adapter/platform: отдельные обязанности

Проверены все source occurrences7A6/1958,7B9/1977,7D0/2000 в all-active:

- RamFan1p5.SetOverBoostByDynamicTemp:2863–2873 —7A6.1 черезmaskFD, сохраняет30.
- Legacy fan NeedSync —7A6.0; touchpad LED —7A6.3 черезmaskF7;
  TouchpadToggle —7A6.6 черезmaskBF. Они не включают Healthy/проценты.
- Native CPU/FAN SetUserProfile не вызывает BatteryProtection2 и не пишет её
  mask30. AC/DC power routing вызывает MyFan.PowerStatusChange, а не charging
  protection policy. `PowerModeEvent:55–73` трактует TypeCstatus18/36 как DC
  для CPU/GPU performance, не как charger stop command.
- WKD_CriticalBatterySetting.HardCodeLevel поднимает **Windows critical action
  threshold минимум до5%** вAC/DC планах. Это OS low-battery policy, не верхний
  предел зарядки и не EC critical-register setter.
- Не найдена exact host цепочка battery policy → ROMID770/platform7C3/741/742
  дополнительный activation. Их назначение в другом ноутбуке не переносится.

OEM Read/Write берут monitor по отдельности, а не на весь RMW. Следовательно
параллельные async callbacks потенциально способны потерять соседние изменения
общего7A6 из-за stale byte. Это риск реализации host; в сохранённых данных такой
race не доказан и не объявляется причиной зарядки выше85%.

## 9. Отдельный новый gap текущего MagicControl

Read-only просмотр production source подтвердил сообщение root:
`SettingsPage::writeBattery:1471–1475` вызывает только
`NativeBatteryController::setExperimentalChargeLimit(100/95/85)`.
Публичный native `set(BatteryProtectionMode)` существует, но caller из текущего
GUI не найден. Native saved BatteryProtection может восстановиться startup/resume,
а percent experiment подавляет older native restore в текущем controller lifetime.

Таким образом нынешние100/95/85 кнопки **не испытывают Healthy**. Нельзя оценивать
Healthy по эксперименту85/80 сraw7A6=01. UI/API gap и пропуск GCUBridge общим
battery owner helper — отдельные production вопросы; в этом исследовании код
не изменялся и недостающие firmware функции не подменялись UI правкой.

## 10. Строгий итог и оставшиеся пробелы

**Усиленный независимый вывод подтверждает прежний:** вызываемый exact OEM
HEALTHYMODE host-path заканчивается изменением7A6.mask30; применимой к GM6
host-цепочки private percentage helpers/UEFI gate не обнаружено даже с проверкой
сырого IL, async/delegates и dynamic binder names.

Это не равно утверждению «Healthy ничего не делает». Родной UI обещает защиту
от высокого напряжения, а firmware может регулировать напряжение и ток без
видимого SOC=85%. Параметры ниже остаются **неизвестными**:

1. Исполняемый consumer7A6.4/.5 в exact EC/BIOS/SMM и допустимость policy на этой
   прошивке/батарее/адаптере; общий enable/capability вне host-кода.
2. Реальные CV/CC целевые значения по трём профилям, переходность, время применения,
   доступность firmware chargingIC и условия stop/restart.
3. Потребляется ли7B9/7D0 как regulation thresholds, только как ACPI reporting,
   либо требует дополнительного firmware состояния.
4. Взаимодействие native voltage policy и процентной пары. Нельзя назначить
   Balanced=95 или Healthy=85 по названию или смешать эти две API.
5. Физический Healthy/Balanced опыт с доказанной записью/сохранённым raw7A6,
   реальным SOC/remaining/full/current/voltage и временной привязкой. В текущих
   разборных логах/дампах такого опыта нет; аппаратные действия здесь не выполнялись.

Имеющиеся RAM/ACPI/UEFI-state dumps полезны для проверки состояний, но сами
не дают исполняемый код отсутствующего chargeIC consumer. Root отдельно
ведёт firmware/ACPI/архивный census; результаты этого файла не объявляют
последнее звено найденным.

## Воспроизводимость и пределы отрицательных выводов

Свежий metadata-only шаг:

`dotnet build/research-hydroc16g2/decompiler/bin/Debug/net10.0/Decompile.dll build/research-gm6ir0c/samples/GCUService-active.exe --names research/devices/gm6ir0c/battery-all-modes-20261021/oem-audit/token-names.json`

Затем Python `audit.py` читает точный PE, существующий metadata index, свежие
token names, framework opcodes и сохранённые sources. Никаких imports OEM,
Assembly.Load/Reflection-only invocation её кода или product tests/build.

Не исключаются вызовы из отсутствующей другой сборки, вычисляемые во время
исполнения reflection strings, неизвестный native code или firmware. Поиск
text reflection в all-active ограничен присутствующими файлами; он нашёл
framework/PInvoke delegate infrastructure, но не private battery invocation.
Поэтому вывод сформулирован как отсутствие найденной применимой цепочки в
конкретном complete exact assembly, а не универсальная недостижимость при любой
возможной внешней инъекции/прошивке.
