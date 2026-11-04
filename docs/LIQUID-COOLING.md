# HYDROC: ядро управления внешним LPP

24.10.2026. Подключён API ядра без новых карточек/кнопок. Реализация разделена:
`LiquidCooling.h/.cpp` — состояния, последовательности и автоматика;
`protocols/uniwill/hydroc16g2/LiquidCooling.h` — OEM packets/кодеки;
`platform/windows/LiquidCoolingWindows.cpp` — BLE/GATT и native policy bridge.
Linux имеет явный unavailable adapter, без guessed BLE/EC writers.

## Подключение

Фабрика `createLiquidCoolingController()` допускается только на фактическом
HYDROC-16 G2 и возвращает инертную сессию. Создание не сканирует эфир, не
подключается и не восстанавливает сохранённые режимы. Другие модели/`--no-ec`
возвращают nullptr. Автоподключения/включения Bluetooth/спаривания/OTA нет.

Явный `discover()` выполняет4с BLE scan и принимает точные имена
`CoolingSystem LCT21001` / `CoolingSystem LCT22002`. Возвращается список адресов;
выбор конкретного ID остаётся вызывающему коду. `connect(id)` принимает только
свежий результат обнаружения (до60с), проверяет имя ещё раз и требует ровно один
native service/characteristic. Нельзя молча выбрать первый похожий блок.

Сервис `6e400001-b5a3-f393-e0a9-e50e24dcca9e`, TX `...0002...` с Write,
RX `...0003...` с Notify. После подписки отправляется ASCII `sw`. Версия
извлекается из текстового ответа с offset17, как Demo.BLE.MergeFWversion;
исходный ответ сохраняется. GATT-connected, Notify-ready и firmware-ready —
разные состояния. Connect не отправляет команды помпы/вентилятора; при этом
автономное поведение прошивки самого блока по одному BLE-соединению не измерено.

Публичные действия вызываются с фонового MTA-worker. UI не добавлен. WinRT
операции имеют локальные deadlines; при timeout команда может завершиться поздно,
поэтому она не повторяется автоматически. Старые callbacks отбрасываются по
поколению соединения. OEM ownership проверяется перед подключением/командами;
службы и процессы программа не останавливает.

## Отдельные команды

| API | Native frame | Смысл |
|---|---|---|
|setPump|FE 1C on duty voltageCode 00 00 EF|Помпа: duty0..100, voltage7/8/11/12; приon duty ненулевой|
|setFan|FE 1B on duty 00 00 00 EF|Вентилятор внешнего радиатора, отдельно от двух внутренних|
|requestMeter|FE 32 00 00 00 00 00 EF|Запрос состояния датчика потока|
|sleep|FE 19 00 01 00 00 00 EF|Явный сон блока, затем закрытие BLE|
|disconnect(true)|Pump Off → Fan Off → FE38...EF → Close|Остановка/line-off до освобождения транспорта|
|disconnect(false)|Close|Разрыв транспорта без команд выходам|

VoltageCode:11В→0,12В→1,7В→2,8В→3. Обычный OEM Auto использует duty помпы60;
в fill-сценариях OEM встречается100. Наша автоматическая заливка не реализована.
Пакеты построены чистым codec и проверяются в транспортном allowlist; firmware
update, произвольные UART bytes и LPP RGB этим API не разрешаются.

Успех GATT WriteWithResponse — приём команды транспортом, не измеренный RPM,
фактические проценты или подтверждённая остановка. В notifications FE31/FE32
состояние берётся изbyte3; OEM считает2 нормальным потоком. Неизвестное/устаревшее
значение не становится нормальным/нулём по умолчанию. В diagnostics остаются
последние raw notification, firmware reply, intent принятых команд и uncertainty.

## Автоматический режим

`enableAutomatic()` — явное действие. До команд проверяются exact BIOS
N.1.13ELU11, собственный установленный SYS pin11.2.18.347, Project26/P13/RAM1.5,
LC capability7C8.5, отсутствие OEM и чужого presence7C7.1. Нужны уже выбранные
FAN Default и CPU Auto: чужие Manual/online/Max/PL не сбрасываются ради LPP.

После admission отправляются явные OEM default Pump60/8В и внешний Fan50.
Запрашивается свежий meter, до3с ожидается normal2. Без этого native policy
не включается; принятые начальные команды отражаются отдельно, Auto остаётсяfalse.
Для старых блоков без подтверждаемого meter остаётся отдельное ручное управление.

Затем общий typed HYDROC controller захватывает только7C7.1 свежим masked RMW
с readback. Соседние HWOC-биты и платформа13 сохраняются. Cleanup responsibility
фиксируется ДО IO, чтобы failed readback не выдавался за отсутствие записи.
Чужой заранее установленный presence не присваивается нашей сессии.

OEM WMILC_Condition вызывает GetSetULong с постоянным Data5497558138892
(0x5000000000C); его bool onoff в восстановленном коде не используется. В нашей
реализации это отдельные fixed helper-операции On/Off с ожидаемым фактическим
presence-предикатом. Child повторно проверяет BIOS/driver/project/cap/presence,
читает EC и вызывает только этот метод; EC/BLE setters в child нет. Parent
ограничивает helper5с. Это control-notification scope, не обычный READONLY inventory.
Если вызов не завершён/не подтверждён, автоматического повтора нет; собственный
бит снимается проверенным cleanup, его ошибка сохраняется.

Через5,5с после включения policy и далее раз в20с читаются native задания:
7E7 — помпа,7E6 — внешний вентилятор. Бит7 разрешает значение low7; при снятом
бите OEM возвращаетoff. Неподтверждённые voltage-коды и duty>100 запрещены,
не clamp-ятся в правдоподобные значения. Передаются только изменившиеся цели.
Поток запрашивается раз в2с; после grace3с работающей помпы abnormal/stale>6с
снимает Auto и собственную LC policy, без слепого выключения помпы/повтора команд.
Проверка BLE/потока и cleanup выполняются перед обычной background-очередью:
занятая UI-команда не откладывает это решение. Реальный masked RMW при этом
остаётся сериализованным общим typed EC controller.
WMI hotkey/boost callbacks и новый UI этим этапом не подключаются; native цели
обслуживаются периодическим ядром.

## Потеря связи, сон, выход

BLE loss отменяет Auto, освобождает только нашу LC policy и оставляет physical
pump/fan state unknown. Разрыв BLE сам по себе не объявляется остановкой.
`stopCooling()` снимает нашу policy перед Off-пакетами. `disconnect(true)` сначала
отправляет Off/line-off, затем закрывает сервис; обратной гонки OEM async-close
перед отправкой line-off в нашем коде нет.

Sleep/suspend/resume/exit ставят запрос существующей сессии в worker: прекращают
Auto, снимают свою policy, закрывают BLE. Это не сбрасывает внутренние FAN/PL,
не отправляет Stop выходам автоматически и не создаёт новую сессию. После
возобновления нужен явный connect; конфиг/автозапуск не сохраняется. При ошибке
cleanup owned/uncertain остаются в состоянии для дальнейшего разбора.

`state()`/`tryState()` различают transport, ready, принятые intent и meter;
`tryState()` не ждёт долгую команду. В дампе `liquid-cooling.json` сохраняется
только cached software state либо session_created=false: сам дамп не запускает
BLE discovery, помпу или native policy.

## Evidence и границы

Собственный ControlCenter5.23.51.56: Demo.BLE02000193 и LiquidCooling02000217.
UUID/name — BLE584–592; notify/meter961–995; firmware offset999–1021;
WriteWithResult1049–1062; native targets/condition2680–2729; Auto2812–2846;
Sleep/line-off2871–2888; Fan/Pump2893–2931. Async GATT-тела разобраны из
восстановленного IL:060008B7/060008BB; строка7000E10E декодируется в `sw`.
Хэши и ключевые строки — research/hydroc-lpp-core-20261024.

WinRT connect/UNCACHED discovery/close следуют [Microsoft GATT client](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/gatt-client).
Документация Windows предупреждает, что connection/IO может завершиться позже
локальной отмены; наша модель поэтому хранит uncertainty и не делает autoplay.

OEM/прошивки/приложение и физические EC/BLE пробы помощником не запускались.
Поддержка реализована и компилируется, физический эффект/feedback/cleanup на LPP
ещё требуется подтвердить владельцем. UI, LPP RGB и обновление firmware не добавлены.
