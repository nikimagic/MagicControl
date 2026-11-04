# GM6IR0C: анализ архива и подключение карточек

21.10.2026. Исходник владельца: `logs_GM6IR0C (4).zip`, 4381582байта,
SHA256 `29cc4815be2833e282b64c4ac032d8e3fbb352a4b0038172255372b01ba41a7d`.
Статически извлечены455 файлов,60179533байта, список с SHA — archive-manifest.json.
ZIP/полные журналы остаются локальными, исходные файлы не выполнялись.
inspect.py воспроизводит initial-archive-summary.json из распакованного архива.

## Что исследовано

Точная плата MECHREVO JiguangX Series GM6IR0C, Project25, RamFan1.5 (78E.6),
Intel i7-14650HX, NVIDIA RTX4060 Laptop. Присланная программа —0.1.118.
Основной сеанс21октября16:28:24–17:54:57 UTC; три завершённых power-input этапа
и обычный финальный dump. В трёх этапах4096/4096 EC byte-valid, >=60с
системных samples; полные raw snapshots не являются атомарным аппаратным снимком.

Сверены локальные первичные источники:

- `build/research-gm6ir0c/samples/DSDT.dsl`: ECXP=ECMA+400, XIF/XST, _BIF/_BST.
- `decompiled-oem-active/MyECIO.MyEcCtrl.decompiled.cs` и активный RamFan1p5.
- `all-active/020002E3-MyControlCenter.BatteryInfo.cs` (циклы4A6/alert494).
- `battery-active/GCUService.MySystem.BatteryProtection2.cs` (policy7A6.mask30).

Это хостовый OEM-код и DSDT, не исполняемая прошивка EC.

## Батарея

XIF0=1: мА·ч/мА. Design402=5200, full404=4300, cycles4A6=42 во всех трёх этапах.
EC full/design=82,6923077%. Pure Windows full/design=60060/60060мВт·ч,100%.
Причина подтверждена DSDT: при ISDB497.0 и cycles<50 _BIF заменяет full на
design; _BST масштабирует remaining. Это объясняет расхождение, не измеряет химию.

| Сохранённый этап | State432 | Remaining мА·ч | Current434 мА | Voltage438 В | EC U×I Вт | XCIN/AC |
|---|---:|---:|---:|---:|---:|---|
| Разряд | 1 | 4300 | 4047 | 11,636 | −47,090892 | Нет |
| Зарядка DC | 2 | 3268 | 2006 | 11,822 | +23,714932 | Да |
| Полная DC | 0 | 4300 | 0 | 12,361 | 0 | Да |

Windows dynamic sample снимается позднее EC RAM, поэтому близкие значения не
обязаны совпадать до последнего разряда. Смешивать несинхронные U/I нельзя.
ISDBbit7 коррелирует с полным зарядом, самостоятельный смысл не установлен.
Температура: Windows BatteryTemperature возвращает ошибку1; собственного
подтверждённого EC temperature поля в исследованном GM6 коде нет.
4A2 Wujie не переносится. 7CC/CD/CE=0 и наDC: ноль не означает отсутствия AC;
активность берётся из XCIN43C.0, BPST/BSOK —490.1/.2.
Единицы sideband PD/PMAX/PBSS остаются неподтверждёнными.
Пара7B9/7D0=0/0 не подтверждает лимит100%; BatteryProtection2 — отдельная политика.

Реализация: точный read-only reader, повтор полного набора и identity,
pure decoder, общий snapshot/cache без controller/restore/setter. SETs EC
карточка63 строки со своим каталогом27регистров/6битов. Windows27 строк остаётся
pure OS. Чужие X5 raw кандидаты убраны. Журнал изменений/целых SOC/10с получает
тот же EC snapshot и имена полей; три пары файлов по запускам сохранены.

## CPU и GPU

Override783/784/785=0/0/0 в новых снимках: CPU Auto, не0Вт.
Fresh Office734=45/45/200, Gaming730=75/75/200, Turbo7A7=140/165/250;
PL4Double727.7=0. Карточка использует свежие OEM triples, не константы архива.
Первый общий CPU slot GM6 — «Авто», reset только трёх override через уже
существующий проверяемый writer; FANs/751/manual draft не затрагиваются.
CPU Package — существующий Intel RAPL read-only meter с собственным источником;
PL readback не доказывает реальный enforcement под нагрузкой.

FANs GPU display раньше следовал адаптеру вывода, поэтому показывал Intel UHD,
когда NVIDIA рендерила. Общий выбор теперь по максимальной доступной валидной
загрузке. При равенстве dGPU/стабильный ID; unknown не считается0.
Физический EC fan2 остаётся прежним. Нового polling/wake/control нет.

NVML final enforced=120000мВт, default115000,max140000,limit=null,
power_control_reported=false. GPU-Z119,6Вт с этим согласуется. Max140000 —
верхняя сообщённая граница, не доказательство активных140Вт или снижения программой.
Raw590006мВт фильтруется как аномалия; её причина из архива не доказана.
NVML fan_count0 не означает отсутствия EC вентиляторов. Финальный Max30с
показал4418/4261RPM и подтверждённый возврат; проверено actual restore состояние,
не устаревшая строка report template. Новых аппаратных проб не было.

## Проверки и ограничения

Три исходных4096байт RAM+validity сохранены в tests/offline/fixtures/gm6-*,
provenance хранит исходные ZIP members и SHA. Тесты чистого decoder проверяют
все три состояния, отсутствие чужих/неподтверждённых измерений, invalid/not-ready
guards и journal change/SOC. Тест карточек проверяет реальные Windows/Linux
model adapters и load-based GPU selection. Windows5/5, Linux4/4 offline suites
прошли. Изолированный native paint тест не запускает MagicControl.

Запуск новой программы на GM6, физические cutoff/restart, CPU enforcement и
визуальный результат на его экране здесь не проверены. EC/SMBus/драйверные
команды не выполнялись. Доставка версии/хэши — отдельный verification отчёт.
