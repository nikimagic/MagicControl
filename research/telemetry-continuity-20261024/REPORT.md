# Исправления по полному анализу GM6 и единая непрерывность данных

24.10.2026. Итоговая версия0.1.184. Базовые исправления собраны/закоммичены
в183 (`a4b34b9`);184 добавляет проверку известных serial/uniqueId батареи и
корректную исходную метку первого GPU power-setting с процентной единицей.
Сборка/версии/SHA256 итоговых portable — `portable-verification-184.json`.

## Выполненное поручение

Владелец попросил исправить выявленные дефекты для всех ноутбуков, затем
уточнил: при недоступном EC использовать прошлые показания на уровне ядра.
Политика реализована в общем `DisplayContinuity.h`, не отдельными механизмами
для моделей. Подключены Windows/Linux FANs/SETs/KEYs/GPU страницы.

| Область | Реализация |
|---|---|
|Последние числа|Общий retainDisplayValue: отсутствующий/нечисловой ответ удерживает прошлое;0/false обновляют данные|
|FANs|MonitoringDisplayCache удерживает CPU/GPU/temp/RPM/PWM/RAM/disk/power/NPU/SoC, sources и времена|
|GPU страницы|GpuDisplayCache удерживает измерения/лимит/range/settings для того же ID/единицы; permissions остаются текущими|
|Отдельная EC батарея|EcBatteryDisplayCache удерживает полный прошлый coherent snapshot с retained flag/исходной датой|
|Typed данные|LaptopDisplayCache удерживает поля HWMI/sysfs, не connected/writable|
|Клавиатура|KeyboardDisplayCache сохраняет последний UI снимок при временном отказе; control availability не берётся из кэша|
|KEYs|Последний известный выбор отображается при неизвестном следующем ответе; available/error отдельно|
|Здоровье|Core retainBatteryReadings сохраняет прошлую EC оценку/source/time при transient healthReadUnavailable|
|Идентичность|Известные deviceId/serial/uniqueId и физические GPU IDs не смешиваются; удаление сбрасывает данные|

После согласованного нового ответа данные обновляются. У ещё не читавшегося
датчика нет выдуманного значения. Кэш в памяти текущего запуска; это история
отображения, не новая аппаратная телеметрия. Согласованная невалидная ёмкость
отличается от временного пропуска и инвалидирует здоровье.

## Подсветка

Общий SingleZone setter останавливает rainbow767.7 и pulse7C5.low3 перед
явным level0/OFF, подтверждает итог и сохраняет прежний owned rollback.
Не затрагивает7C5.7/FAN Respective,767.4/USB,767.1/lightbar.
Явный OFF также останавливает host Cycle6 и сохраняет его disabled intent.
Следующий явный ON возвращает сохранённый native effect/RGB через прежний setter.
Host cycle автоматически не запускается. Native idle powerDisabled-only и
точный временный snapshot restore сохраняют эффекты; startup saved level0 не
возвращает Pulse над выключенным питанием.

Применяется ко всем уже допущенным SingleZone профилям через прежние identity/
capability/ownership guards. Honor white и HYDROC HID сохраняют собственные
протоколы OFF; чужие регистры им не добавлены.

## Батарея и здоровье

При паузе/ошибке GM-family EC оценка76.9% не заменяется Windows/BIF100%.
HealthStale/source/исходное healthAtMs сохраняются; следующий свежий EC ответ
заменяет значение. OS подробности продолжают показывать собственные значения.
Windows BatteryInformation read failure и Linux missing sysfs capacity отделены
от согласованной невалидной ёмкости. Значения и возраст retained полей есть в логах.
Пороги, ROM-ID, платформа, native Healthy и другие батарейные writers не менялись.

## GPU и журналирование

- Windows primary привязан к активному source LUID/GDI из QueryDisplayConfig;
  shared DXGI HMONITOR больше не делает рендерящий dGPU основным дисплеем.
- Linux boot_vga сохраняется отдельным полем; неизвестный desktop primary
  сериализуется как null с primary_known=false.
- NVML0 при противоречащем аномальном instantaneous/query не принимается за0Вт.
  Raw ответы/ошибки сохраняются, UI может показывать последнее валидное значение
  с retained metadata. Расход/P0 NVIDIA не исправляется выдуманным числом.
- В FANs.presentation есть выбранный UI adapterId, selectionReason, все load
  candidates и retained field times; fixed cooling snapshot остаётся отдельным.
- Windows MC.Selected и GTK primary входят в snapshots. SETs пишет выбранные
  brightness/upper индексы, retained EC/keyboard и полный7C5. GPU page пишет
  отображённые числа отдельно от raw API и текущих прав управления.
- Queued/callback outcome отделён от readback. Пропуск одного battery поля больше
  не подавляет события свежего SOC; смешанные возрастные показания явно маркируются.

## Покрытие моделей/ОС и границы

Wujie, GM6 и другие уже подключённые Uniwill-профили используют общий SingleZone
и cache. Honor/Huawei используют общий cache поверх своих typed данных. HYDROC
сохраняет свои HID/EC допуски. Catalogue-only/Unknown используют реальные OS
источники и не получают неподтверждённого EC управления. Общая политика относится
ко всем16 каталожным моделям; это не физическое испытание16 ноутбуков.

Кэш применяется только к публикации UI. Manual online, защита от потери датчика,
setters, контрольное чтение и raw diagnostic collectors продолжают использовать
свежие исходные данные/свои deadlines. Запрет/ошибка подключения не превращаются
в разрешение записи из-за оставшихся на экране старых чисел.

Координаты/размеры/высоты карточек, scrollbar/gutter/Y, шапка и renderer не менялись.
Не выполнены установка/смена драйверов, настройки NVIDIA clock/P-state, запуск
приложения/helpers, аппаратные пробы, тесты приложения или EC/flash операции
помощником. Физическое гашение/исчезновение визуального моргания требуют нового
пользовательского наблюдения. Причина высокого P0/потребления NVIDIA, реальная
отсечка/гистерезис и неподдержанные драйверные setters не объявлены проверенными.

Билд обеих ОС выполнен общим Tools/Build-All.ps1, portable обеих ОС обновлены
и сверены по четырём GUI/CLI SHA256/версиям. GitHub архивы не готовились.
Личные INI/CSV/логи/дампы и параллельные Wujie SPI research/docs исключены из коммитов.
