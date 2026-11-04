# Инвентаризация телеметрии MagicControl 0.1.408

Дата: 30.10.2026. Срез исходников: `a7388e4b`. Это статический анализ; работающие
процессы, интенсивность обращений, время вызовов и поведение оборудования в этом
исследовании не измерялись. Интервалы ниже — условия в коде, а не гарантированная
частота успешных физических измерений. Поведение зависит от модели, ОС, вкладки,
журналирования, занятости worker и доступности источника.

## Входы и потребители

| Потребитель | Источники / данные | Когда вызывается | Основной код |
|---|---|---|---|
| FANs Windows | `MonitoringReader`: батарея, CPU/GPU/NPU, память, диски; отдельный `readFanTelemetry` и модельные контроллеры вентиляторов | Таймер 1 с при видимой странице; скрытая страница без логов не опрашивает, при обычных логах ограничение 5 с; battery logging/диагностический burst меняют условие. Занятый worker пропускает повтор | `src/gui/windows/FansPage.cpp:83`, `src/platform/windows/FanDeviceActionsWindows.cpp:546`, `src/platform/windows/FanMonitoringWindows.cpp:8` |
| FANs Linux | Собственный `MonitoringReader` и `ResourceState` | Таймер 1 с при открытой странице; при выключенном battery logging таймер снимается при скрытии. Обычный полный `MonitoringReader::read` возвращает свой кэш на 2 с; diagnostic обходит ограничение | `src/gui/linux/FansPage.cpp:429`, `src/platform/linux/FansServicesLinux.cpp:9`, `src/platform/linux/MonitoringLinux.cpp:380` |
| SETs Windows | Свои `batteryReader`, `ApuPowerReader` и Intel reader; дополнительно EC/OEM состояние, батарейные настройки, подсветка и idle | При открытой вкладке таймер 3 с; Wujie таймер 1 с для idle подсветки, полный опрос ограничен 3 с. Команды/readback запускают дополнительные чтения; quick paths могут откладывать телеметрию | `src/gui/windows/SettingsPage.cpp:1443`, `src/platform/windows/SettingsOperationsWindows.cpp:398`, `src/platform/windows/SettingsSnapshotWindows.cpp:178` |
| SETs Linux | Собственный `MonitoringReader` для батареи, отдельно `LaptopControl::read()` | 3 с при отображении; опросы по действиям, открытию и resume дополнительно | `src/gui/linux/SettingsPage.cpp:462`, `src/platform/linux/SettingsServicesLinux.cpp:95` |
| Страницы GPU | Provider выбранной страницы через общий `GpuOperations` и `gpu.read()` | Windows/Linux — таймер 1 с, только отображаемая страница и не занятая операция | `src/gui/windows/NvidiaGpuPage.cpp:134`, `src/gui/linux/NvidiaGpuPage.cpp:91`, `src/platform/common/DiscreteGpuOperation.cpp:34` |
| KEYs | Состояния аппаратных и системных переключателей, не общий снимок датчиков | Windows 2 с на видимой странице; Linux 3 с на отображаемой странице; lifecycle и действия отдельно | `src/gui/windows/KeysPage.cpp:473`, `src/gui/linux/KeysPage.cpp:110` |
| INFO / диагностика / CLI | Inventory и подробные датчики, EC/BIOS/VBIOS dump; CLI создаёт отдельный `MonitoringReader` по своей команде | По запросу; частота и дополнительные чтения зависят от команды. Это не обычный таймер INFO | `src/platform/common/InfoDumpResource.cpp:50`, `src/cli/CurrentCommands.cpp:192` |
| Управление и защита вентиляторов | Свежие профильные температуры/состояние контроллера; отдельные loops, а не UI cache | Wujie worker обычно ожидает до 1 с, может просыпаться раньше по событиям; Honor и HPBSD имеют 1-секундные проверки при владении режимом. Это разные модели и условия | `src/devices/mechrevo/x5sp4nag/windows/FanControlWindows.cpp:183`, `:690`, `src/devices/huawei/hlyl-wxx9/FanControlWindows.cpp:82`, `src/devices/minisforum/hpbsd/FanControlWindows.cpp:173` |

## Какие данные читаются

| Группа | Windows | Linux | Примечания |
|---|---|---|---|
| CPU | AMD ADL/ADLX, PawnIO; Intel PawnIO; Windows `GetSystemTimes` и частота как fallback | `/proc/stat`, cpufreq, hwmon `k10temp`/`coretemp`, RAPL powercap при доступности | Температура, загрузка, частота и мощность имеют разные источники; Package и cores не складывать и не отождествлять |
| GPU | AMD ADL/ADLX, NVIDIA provider, Intel provider; отдельная проекция по идентификатору адаптера и маршруту дисплея | Radeon/NVIDIA/Intel providers, DRM/hwmon при доступности | На многокарточном устройстве сохранять идентичность GPU. Показания общей области мощности не считать потреблением конкретного чипа без подтверждения |
| NPU | AMD XRT: мощность кэш 5 с при успехе, 30 с до повтора при отсутствии/ошибке; остальные данные через доступные AMD источники | `amdxdna` hwmon `power1_input`, если доступен | Не выдавать отсутствие данных за ноль |
| FAN | EC UniWill, HWMI Honor, ITE HPBSD и другие модельные пути; запрос/факт могут различаться | UniWill hwmon `fan*_input`/`pwm*` и модельные данные при наличии | Наблюдение не заменяет свежий readback команды/защитный контур |
| Батарея и питание | Battery Class IOCTL / `GetSystemPowerStatus`, с модельно подтверждённым дополнением EC/HWMI | `power_supply`, иногда hwmon; EC-only метод сейчас возвращает unavailable | ОС, дополненный снимок и независимый EC-снимок имеют разный смысл. Сетевое питание при отсутствии данных остаётся неизвестным |
| Память | SMBIOS идентичность; поддержанные PawnIO SMBus/SPD5118 | `spd5118` hwmon, SMBIOS для идентичности | Распаянная память не получает выдуманный серийный номер/температуру |
| Диски | Storage IOCTL и NVMe SMART при наличии | sysfs NVMe + hwmon | Windows обычный cache 5 с; diagnostic его обходит |
| Мощность APU/лимиты | PawnIO RyzenSMU / модельный EC; данные читаются в SETs отдельно | Возможности зависят от драйвера и модели | Измеренная мощность, запрошенные ограничения, значения EC и SMU — разные величины |
| Сеть, звук, камера, устройства | INFO inventory системных устройств | INFO inventory системных устройств | В основном идентификация/инвентарь, а не секундная телеметрия |
| Подсветка и KEYs | EC/HWMI и системные API согласно профилю, включая idle Wujie | OS/sysfs/HWMI backend по возможностям | Нельзя объединять таймер подсветки с состоянием APP или переносить EC-карту между моделями |

Основные реализации: `src/platform/windows/MonitoringWindows.cpp:180`,
`src/platform/linux/MonitoringLinux.cpp:194`, `src/core/include/MagicControl/Monitoring.h`,
`src/platform/windows/AmdTelemetryWindows.cpp`,
`src/platform/windows/pawnio/PawnTelemetryWindows.cpp`,
`src/platform/windows/pawnio/ApuPowerWindows.cpp`.

## Кэши и свежесть

* Windows: перечисление Battery Class — 10 с, при ошибке повтор может быть раньше;
  серийные номера памяти — 1 мин, диски — 5 с вне diagnostic. Кэши принадлежат
  экземпляру `MonitoringReader`, а не всему процессу. Сохранённые динамические
  батарейные поля имеют исходные времена и ограничение 10 с; health имеет
  собственный статус устаревания. Не копировать их в контрольный readback.
* Linux: обычный полный снимок reader кэшируется 2 с; отдельные `readBatteryOnly`
  и `readOsBatteryOnly` этот кэш полного снимка не используют. В конце полного
  опроса есть fallback к прежним RPM/PWM при пропущенном значении
  (`MonitoringLinux.cpp:461`); срок и происхождение этих значений при передаче
  потребителю нужно проверить до объединения кэшей.
* Wujie EC 0x44C — экспериментальный кандидат системной мощности с предположением
  raw=W; кэш истекает через 10 с. Оценка после вычитания зарядки отдельно от
  исходного поля. Не использовать её для контроля.
* Наличие времени software-observation не означает аппаратной метки датчика.
  Различать свежесть каждого показания и время завершения всего сборщика.

## Подтверждённые повторные пути и кандидаты

1. **Двойное чтение системной батареи в SETs.** Windows `readBatteryOnly()`
   вызывает `impl_->readBattery()`, затем `readOsBatteryOnly()` снова вызывает
   `impl_->readBattery()`. На Linux оба метода тоже отдельно запускают
   `readBattery`. Это два вызова системного чтения при обычном полном проходе,
   когда monitoring разрешён. Независимое EC-only чтение не считать дублем.
   Возможный вариант — один сырой OS-снимок и две проекции, но сохранить
   последовательность, partial failure, идентичность батареи и журнальные события.
2. **FANs и SETs создают отдельные battery readers.** При совпадении их активности
   оба могут читать один Battery Class / power_supply, причём с разными локальными
   кэшами. Реальная частота зависит от видимости и журналов: измерить до правки.
3. **GPU providers повторяются между FANs и GPU page.** FANs собирает display
   projection через GPU provider, GPU page периодически использует свой
   `GpuOperations` provider. Совпадение во времени зависит от видимости/логов и
   оборудования. Проверить идентификаторы устройств, режимы и стоимость вызовов.
4. **Потенциально повторное чтение AMD в одном Windows FAN frame:** базовый
   `amd.read(sample)` и `displayRadeon.read(true)` при AMD display projection.
   Оба проходят через соответствующие адаптеры; конкретное совпадение драйверных
   вызовов и данных требует инструментального подтверждения, не считать уже
   доказанным тождеством значений или стоимости.
5. Wujie Manual online читает управляющие температуры независимо от UI. Honor
   guard также читает свою CPU температуру, HPBSD имеет отдельный watchdog.
   Это преднамеренные защитные пути; убирать их в пользу presentation cache нельзя.

## Что измерить до внедрения общего снимка

* На Wujie, GM6/GM5, Honor и HPBSD, отдельно Windows/Linux по реальной поддержке:
  частота и длительность вызовов по source+physical ID, ошибки, задержка очереди,
  CPU, память, handles и источник времени. Сценарии: FANs/SETs/GPU видимы,
  скрыты, logging on/off, sleep/resume и переключение адаптера питания.
* Считать вызовы без изменения аппаратного поведения; отдельно измерять холодную
  и прогретую работу. Не писать ключи/ввод, не собирать изображение камеры.
* У будущего общего снимка — идентичность устройства и источника, значение,
  timestamp, состояние fresh/stale/unavailable, причина ошибки и поколение после
  сна/смены устройства. Не объединять несовместимые значения или разные батареи.
* Команды, readback и FAN safety сохраняют право на свежий целевой опрос с
  текущими gate/ownership и приоритетом; UI/logging подписываются на измерения.
  Не продлевать timestamp при повторной выдаче старого значения.

Исходники/настройки/оборудование не изменялись; тесты и приложение не запускались.
