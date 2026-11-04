MagicControl0.1.104 — подробные сведения батареи и журнал SOC/FLAGS

Запрос владельца:
На SETs, под прежним ограничением зарядки, одна большая карточка: рядом
компактная батарея как на FANs и подробные данные с источником каждого значения.
Добавлены60 строк: OS native battery information, отдельный EC sample, raw
связанные флаги, оценки времени до разряда/100%/настроенного порога, PawnIO status.

Windows: Battery Class IOCTL, без EC-подстановок в правые OS строки. Ёмкости
в native mWh/relative units, manufacturer/model/serial/chemistry/date/unique id,
status/capabilities/technology/alerts/critical bias, SOC/rate/voltage/current(W/V),
temperature/cycles/health, BatteryEstimatedTime при доступности. Linux показывает
свои power_supply поля; energy/charge подписаны mWh/mAh, неподдержанные поля—.

EC: только Wujie82/72/26, тот же EcAccess/transaction, два одинаковых коротких
набора с максимум3 попытками. Все значения EC-only без OS fallback. SOC4AB,
I434,U438,P=I*U,state432,temp4A2,cycles4A6,remaining436/full404/design402.
Заводская/full BMS/remaining ёмкости независимы от нормализации Windows в AML.
Показаны upper/restart/Reached/platform/readiness/ISDB/charger/rating/raw flags.
520/522/BE030E оставлены raw без недоказанного назначения. Флаг/записанный порог
не равен физически остановленной зарядке. Код текущего зарядного ограничения
и его сохранение/восстановление не изменялись.

Время:
Windows native BatteryEstimatedTime для разряда, иначе расчёт по доступной
ёмкости и мощности. EC — ёмкость/I; до100% использует full-remaining, до порога
full*threshold/100-remaining. Активное направление обязательно, при нулевом I
или неизвестной ёмкости—. Выше порога показывается числовое сравнение, не cutoff.
Прогнозы зависят от текущей нагрузки/тока и taper, это не обещанное время.

PawnIO:
Текущий adapter(src/platform/windows/pawnio/PawnTelemetryWindows.cpp:18)
экспортирует SPD/CPU/SMU, не отдельное Battery API. Поэтому батарейная строка
"Не подключено", без подмены данными OS/EC. Не выполняли SMBus/BMS probing,
сканирование адресов, загрузку экспериментального модуля или запись контроллера.

Журнал:
Один baseline process/device/source, затем SOC при изменении отображаемого
целого процента. UTC/elapsed_ms даёт штатный JSONL logger. previous/current/delta,
skipped_percent_samples,W,V,mA,C,cycles,source,device и наблюдённые флаги.
FLAGS отдельно при изменении charge/discharge/AC/critical/upper/restart/Reached/
platform/policy/charger/rating/520/522. OS,EC и смешанная FANs карточка разделены
по stream. Пропущенные проценты не синтезируются, stale не считается новым.
Background FANs poll сохраняет прежние5с; видимая страница — штатный интервал.
Это журнал наблюдений, не аппаратный захват всех событий между опросами.
SETs/FANs разделяют EC sample до800мс, timestamps не обновляются при копировании.

UI:
BatteryDetails.h и setsGeometry общие Windows/Linux, RU/EN/SC/TW подписаны.
Использованы прежние batteryCard/batteryCardGeometry/палитра/шрифты Wujie.
Существующие FANs/KEYs/INFO/scrollbar mechanics не изменены. Сдвиг нижних SETs
секций обусловлен явно запрошенной новой карточкой, прежние контролы сохранены.

Поставка/проверки:
Общий Release0.1.104 Windows/Linux; после штатного выхода владельца обе рабочие
portable обновляются общим launcher. Подтверждение готовности —
verification-0.1.104.json: input hashes, GUI+CLI build/portable versions/SHA256,
личные INI/CSV и защищённые UI файлы. Сборка сначала остановилась на занятом
EXE; дальнейший compile-fix заменил отсутствующий MAXULONG на numeric_limits.
Windows и Linux пересобраны из окончательных исходников в том же номере.
Tests/GUI/EC hardware/OEM программы не запускались помощником. Результат
компиляции не выдаётся за визуальную или физическую проверку новой карточки.
GitHub архивы/публикация не выполнялись; личные настройки/логи/дампы вне коммита.
