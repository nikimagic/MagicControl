# Батарея: общий путь GUI, CLI и восстановления

База `8c1352df`, общий продукт0.1.425. Пакет от01.11.2026. Поручение:
единый путь батареи, удаление старых paths/дублей, все модели и обе ОС,
без изменения геометрии/скролла и аппаратных запусков; offline tests,
Windows/Linux build, реальные working portable и local commit.

## Что было разнесено

- GUI вызывал public applyBattery* policy wrappers, CLI — более низкий resource
  entry и отдельный raw charge-pair helper. Часть завершения CLI выполнял сам.
- Honor Windows и Linux имели две policy реализации с разными входами/чтениями.
  Транзакция чтение/решение/запись/сохранение была представлена разным кодом.
- Native Windows controller уже сохранял thresholds, затем GUI повторял flags.
- Wujie GUI, CLI и внешний restore сохраняли observed pair в разных местах.
- SettingsRememberChoice::Battery оставлял второй GUI settings route.

## Реализация

BatteryWriteRequest — единый переносимый контракт. Процент, нижний порог,
native pair, protection/voltage, UI view, expert platform/ROM policy, native
Turbo и aging остаются разными операциями. Preset/PolicyButton переводятся
в существующем Windows owner; presentation flag не даёт аппаратного доступа.
Пустой request не означает сброс порогов.

Windows GUI/CLI/внешний Wujie restore входят в executeBatteryResource(context,
request). Реальные adapters сохраняют masks, identity, ownership, readback,
timing и rollback. Completion Wujie после checked writer читает реальную пару
и сохраняет её один раз. Lower сохраняется отдельно после подтверждения.
Ни GUI, ни CLI, ни restore не добавляют второй writer. Native controller-owned
threshold/lower persistence не повторяется frontend-ом.

Portable executeBatteryResource(LaptopControl,request,actions) — одна native
policy для обеих ОС, только exact implemented chargePolicyId. Её transaction
охватывает fresh narrow telemetry, выбор lower, checked setter и save. Для upper
Windows использует тот же narrow getter, который уже применялся для restart,
а не full read с I2EC подсветкой. Backend всё ещё проверяет фактический доступ.

Native Upper/Restart сохраняют прежние данные:60 использует lower<=55, не меняя
сохранённый пользовательский lower; legacy10 задаёт5 и сохраняет его. Expert
Pair передаёт оба значения backend-у без policy-read и без перезаписи сохранённого
lower. Его native0/100/0/0 normalization остаётся в backend. Restore origin не
становится новым native user choice. Отказ read/decision/setter не пишет success
в settings; ошибка save остаётся ошибкой после уже подтверждённой команды.

Сохранение выбранного представления и projection подтверждённых bytes общие
для обеих ОС в BatteryUiPolicy. Это storage/view helpers, не hardware admission.
Native view отключает percentage replay intent; Linux не притворяется, что
применил отсутствующий Windows voltage writer. Windows по-прежнему освобождает
процентный режим до native voltage apply: ошибка второго шага не отменяет
завершённое освобождение первого.

## Удаления

- devices/huawei/hlyl-wxx9/BatterySettingsWindows.h/.cpp;
- platform/linux/huawei-wmi/HlylBatterySettingsLinux.h/.cpp;
- public applyBatteryUpper/Restart/Policy/Voltage/View/UsbPowerPriority;
- raw writeChargePairResource и его одноэлементная route table;
- Linux battery wrapper/table и policy-function pointer selector;
- старые Honor actions/apply helpers и неиспользуемый USB-priority reader;
- второй SettingsRememberChoice::Battery route и public persistence wrappers.

Новых production файлов, controller/worker/очередей и compatibility aliases нет.
CLI native limit/restart и Wujie restart/protection используют уже существующий
backend, без новых firmware commands. CLI raw pair теперь проходит exact native
policy admission, а не заимствует charge semantics по одному наличию интерфейса.

## Карта

| Источник | Общий вход | Backend/завершение |
|---|---|---|
| Windows SETs | SettingsAction -> Preset/PolicyButton/обычный request -> executeBatteryResource | прежние exact controllers/EC/core aging; completion внутри owner |
| Windows CLI | actual native/EC context -> тот же request/entry | то же исполнение; нет отдельного completeWujieCommand/save |
| Linux SETs/CLI | portable native request/entry | exact LaptopControl transaction, общий lower/capacity store |
| Wujie startup/resume | savedBatteryWriteRequest + Restore origin | тот же Windows entry и один observed-pair completion |
| Aging restore | Aging request с lifecycle и Restore origin | существующий core aging owner/state/journal |
| Native controller internal restore | существующий model owner | те же private checked apply methods/receipt/rollback; не второй frontend path |

## Сохранённые ограничения

Все actual protocol maps и GUI sources неизменны. Wujie82/72/26, P7 owned ROM04
для допущенного процента и FF при100/native, lower97 и legacy storage сохранены.
Другим platforms/models эти ID не назначаются. GM6/GM5/L16/HYDROC сохраняют свои
controllers и допуски; L16 не получает4/7 switching. Honor не получает Uniwill
байты. HPBSD/Unknown и неподключённые Linux команды остаются недоступными.
Native voltage modes и platform/ROM histories, aging оригиналы/термочасы,
read-only telemetry и display retention не подменяются requested state.
Нижний setter, rollback, autosave и restore не удалены и не объявлены физически
подтверждённым restart только потому, что readback вернул нужный byte.

## Проверки

Существующий architecture_audit.cmake расширен, второго checker нет. Он запрещает
старые wrappers/aliases/files, raw setters вне adapters и дубли CLI/GUI/restore
completion. Это source regression guard, не полный C++ call graph.

Общий offline native test обеих ОС проверяет100/95/85/60/legacy10, lower/pair,
порядок read/decision/write/save, read/write/unwritable/invalid-decision failures,
save failure после apply, Restore origin, raw0/100, policy isolation и transaction
между конкурирующими upper/restart. Windows дополнительно проверяет GUI Preset/
CLI request, общий persisted lower/capacity, скрытую процентную кнопку и отказ
при missing transport для всех17 объявленных профилей. Настоящие controllers
не создаются, только fake LaptopControl и owned test settings file.

В общий offline проект подключён существующий pure battery protocol contract.
Windows7/7 и Linux5/5 включают battery, protocol, shared audit и предыдущие
runtime/CPU/lighting regressions. Старые Windows/Linux policy fixtures адаптированы
и компилируются; их широкие suites не запускаются.

SOURCE-OBSERVATIONS.json:155 сравнений, включая весь GUI/protocol source, реальные
native/EC controllers, ROM enforcement, aging/identity history, FAN bounds,
CPU/lighting implementations и неизменённые view/protection/policy bodies.
CLI за пределами battery command отличается только новой help строкой.

Физическое удержание/restart95/90, фактическое напряжение/зарядный ток и поведение
реальных ноутбуков не тестировались. GUI/CLI, EC/HID/SMU/SYS/OEM/firmware tools
и hardware probes не запускались. Нет CI/license/Git policy/archive/publication
работ. Итог общего build/working portable/4SHA фиксируется в VERIFICATION.json.

Итог: общий Release0.1.426 complete, обе Windows/Linux GUI+mctl working portable
обновлены. Версии/четыре executable SHA-256 проверяются независимо от сообщения
stager; Windows VERSIONINFO согласовано. Pure battery contract выполняет330452
проверки без hardware transports. Личные settings/evidence и чужие изменения
не включаются в product commit.
