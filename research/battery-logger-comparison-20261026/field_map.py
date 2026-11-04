"""Semantic ledger for every logger field; unresolved is an explicit result."""
from pathlib import Path
import json,hashlib,collections
OUT=Path(__file__).resolve().parent;ROOT=OUT.parents[1]
d=json.loads((OUT/'census.json').read_text(encoding='utf8'))
notes={
0x400:('Единицы XIF0','Контракт ACPI/декодера','Определяет допустимость интерпретации ёмкости/показателя тока. Наши exact decoders принимают1; после сброса0 не объявляется автоматически подтверждённым mWh. Отсутствие literal DPTR0400 не отменяет документированный XIF0.'),
0x402:('Проектная ёмкость','ACPI + единицы XIF0','Сырые counts. мА·ч только при подтверждённых единицах; это не остаток и не заданный ток.'),
0x404:('Полная ёмкость','ACPI + единицы XIF0','Оценка полного заряда; full/design даёт расчётную оценку здоровья. Не независимое измерение физического износа.'),
0x406:('Технология XIF3','ACPI GM6','Метаданные, не состояние зарядки и не химия из подписанного ROM.'),
0x408:('Проектное напряжение XIF4','ACPI GM6','мВ; не текущее напряжение батареи и не выходной зарядный target.'),
0x40a:('Предупредительная ёмкость XIF5','ACPI GM6','Порог предупреждения ОС, не пользовательский процент остановки зарядки.'),
0x40c:('Низкая ёмкость XIF6','ACPI GM6','Порог предупреждения ОС; не restart-процент нижнего зарядного порога.'),
0x432:('Состояние XST0','ACPI + собственные EC пути','Отдельно от модуля тока: low2=1 разряд,2 заряд,0 idle,3 противоречие. Bit2 критический статус. В GM6 возможен idle без обнуления434; слово не является прямым CHARGE_EN.'),
0x434:('Опубликованный модуль present rate XST1','ACPI + source trace по платам','При XIF0=1 — модуль тока мА; знак брать из432, не из unsigned слова. GM6v1.12 публикует abs050A; коэффициент34 из selected calculation branches не переносить на WUJIE.'),
0x436:('Остаточная ёмкость XST2','ACPI + единицы XIF0','Сырые counts, возможны ограничения/подстановка firmware. Не подменять ею SOC из другого источника/момента.'),
0x438:('Напряжение XST3','ACPI + EC publication','мВ; V=raw/1000. Это измерительное/опубликованное значение, отлично от030E/0522 и команды15.'),
0x43c:('XCIN/XTIN','Собственный DSDT GM6','Биты внешнего питания. Не переносить на другой адрес WUJIE только по похожему значению.'),
0x490:('ACIC/BPST/BSOK и внутренние статусы','DSDT + EC state machine','Bit0 AC,bit1 основная батарея,bit2 готовность. WUJIE bit3/7 обслуживают выбор/разрешение основной/альтернативной зарядной ветви; это не измеренный поток тока.'),
0x494:('OEM предупреждения/запреты','OEM + EC consumer','Ненулевое значение участвует в stop guards. Конкретный бит должен иметь собственный источник; не считать весь байт одним универсальным fault.'),
0x497:('ISDB и другие статусы','DSDT + разделённые native ветви','Bit0 выбирает отдельный протокол/расчёт батареи. На поле опирается ACPI подстановка ёмкостей; имя ISDB не доказывает физический тип BMS. Остальные биты отдельно не расшифрованы полностью.'),
0x49f:('ADPT: упакованный класс адаптера','DSDT/OEM','Не измеренная мощность потребления и не фактический ток на разъёме.'),
0x4a2:('Опубликованная температура батареи','WUJIE own publication','LE deci-K -> raw/10-273.15. При !BSOK firmware публикует default0B72 (19.85C); готовность обязательна. Полный путь зависит от497.0; не всегда прямое чтение SBS Temperature.'),
0x4a4:('Альтернативная публикация тока','WUJIE bank1 AE43..AEE1','abs внутреннего050E через0A45/46, со своими guards/clamps; при idle путь может обнулить эту публикацию. Название BMS_current_mirror? не доказано; не смешивать с434 или OS Rate.'),
0x4a6:('Счётчик циклов','DSDT/OEM по платам','Не SOC, не таймер зарядки. FFFF недостоверно; замена батареи может менять cycles, без вывода о порче EC.'),
0x4ab:('SOC, опубликованный EC','Собственный WUJIE decoder/firmware compare','Целое0..100; процентная ветка сравнивает его с7B9.low7. Значение не является измерением текущей ёмкости с точностью до процента.'),
0x74c:('BIOS Project selector / status','Own CC codec + valid dumps','Low6 при7CA.0=1 выбирает BIOS_PROJECT_ID. Подпись power_input у WUJIE logger неточна: это не просто байт питания. High bits имеют отдельную роль.'),
0x741:('Присутствие приложения и control context','Own EC741.0 consumer','Bit0 проверяется firmware. В WUJIEC574 и GM6C118 отсутствиеAPP очищает7A6.mask30. Остальные биты не объявляются свободными для записи.'),
0x742:('Capabilities + gate процентной ветки','WUJIE exact EC','Bit2 выставляется/снимается firmware по platform4 OR ROM770=4. Bit5 устанавливается startup-кодом; одно это не доказывает действующую ChargingPriority. Не писать в status вручную.'),
0x770:('Первый байт ROM ID в RAM','Identity + firmware comparisons','Не версия BIOS и не UEFI GUID. ROM0=4 открывает конкретный OR-gate. RAM readback не означает запись/сохранение EC flash.'),
0x7a5:('Смешанные служебные флаги','Own WUJIE EC','Bit2/4 формируются из9063 в input-source routine; bit3 — thermal latch CPU43E>=95 или44F>=85, снимается при обоих<=80. Имена HIGH_POWER/OVERBOOST/LIGHTBAR не являются доказанными универсальными значениями.'),
0x7a6:('Режим native защиты + прочие флаги','Own WUJIE and GM6 EC','Bits5:4 — поле00/10/20. Balanced задаёт минимум100мВ/элемент,Healthy200; старшие aging guards могут усилить снижение. Это напряжение, не процент SOC. Отсутствие741.0 снимаетmask30. Bit1 DYN_TEMP из чужого OEM на эту плату не доказан.'),
0x7ab:('Смешанный preset/config context','WUJIE selected literal consumers','Есть bit0 predicate и разбор нескольких битов в power logic. Нельзя весь байт называть единственным индексом BIOS-профиля без полного codec.'),
0x7b9:('Upper low7 + Reached bit7','Own percent and stop paths','Предел и статус, не самостоятельный CHARGE_EN. Gate закрыт ->Reached очищается. Разобранная native процентная ветка снимаетReached ниже того же upper; нижний restart95/90 этим не доказан.'),
0x7d0:('Host/OEM parameter, native роль не установлена','Own source/census negative scope','Setter Down существует в OEM. В WUJIE DSDT имяDBD1, direct literal07D0 отсутствует в трёх исследованных EC. Ни имяDown, ни маска7F не доказывают restart90. DBD1 и направление generic/indirect accesses требуют отдельного разбора.'),
0x7c3:('OEM platform selector','BIOS writer + EC predicates','Целый байт, не набор battery flags. Влияет и на другие подсистемы. Наблюдаемые4/7/13 не равны автоматически заводской конфигурации.'),
0x7cc:('Статус источников питания и TCSA','DSDT WUJIE + own EC','Bits0/1/2 DC0C/TC1C/TC2C,4/5 TC1P/TC2P,7 TCSA. Bit7 зеркалируется в984.7 и участвует в пересчёте input context. Не универсальный прямой запрет зарядки.'),
0x7cd:('Рейтинг/данные Type-C A','Платозависимый контракт','Не измеренные W без codec конкретной платы. Для GM6 raw units явно не подтверждены.'),
0x7ce:('Рейтинг/данные Type-C B','Платозависимый контракт','Не измеренные W без codec конкретной платы. Для GM6 raw units явно не подтверждены.'),
0x30e:('Базовое желаемое напряжение BMS','Own WUJIE and GM6 native path','BE16 source слова. Профиль здоровья вычисляет target=base-Ncells*delta. Не измеренное напряжение438 и не проценты.'),
0x51c:('Внутреннее слово расчёта ёмкости','Selected WUJIE producers/consumers','Есть чтение и native обновление; полного доказательства постоянного равенства404 нет. Полное назначение/масштаб пока не установлены.'),
0x520:('Native requested-current candidate / copy031A','WUJIE DDBE..DDCF','LE копия BE031A/31B. Не измерительный ток434. Полная low-level принадлежность/единицы031A в этом проходе не закрыты; не назначать мА лишь по похожему числу.'),
0x522:('Подготовленный target зарядного напряжения','Own EC byte chains','LE16 мВ для проверенной цепочки: native CV->522/523->836/837->pending832.1->ChargingVoltage15. Не измеренное напряжение, не ток, не гарантия ACK. На DC может быть сохранённым старым target.'),
0x7d8:('Публикация элемента native power/thermal table','WUJIE bank2 9788','Читается из CODE table[offset14h], не физический датчик. Точные engineering units полного table codec не закрыты.'),
0x7d9:('Публикация элемента native power/thermal table','WUJIE bank2 9795','CODE table[offset20h], не текущая температура батареи. Полная семантика/единицы пока не подтверждены.'),
0x7da:('Публикация элемента native power/thermal table','WUJIE bank2 97A0','CODE table[offset08h], не физическое измерение. Полная семантика/единицы пока не подтверждены.'),
0x753:('Сырой кандидат','Не расшифровано','Нет literal DPTR в exact WUJIE core; computed/indirect use не исключён. Дать имя температуре/току пока нельзя.'),
0x754:('Сырой кандидат','Не расшифровано','Нет literal DPTR в exact WUJIE core; computed/indirect use не исключён. Дать имя температуре/току пока нельзя.'),
0x786:('Host-gated override low7','WUJIE bank2 98BB','Проверяются bit7 и741.0, затемlow7 копируется в power calculation context. Это не подтверждённый датчик TCC; полный конечный SMU effect не установлен.'),
0x787:('Host-gated override low7','WUJIE bank2 8EEE','Проверяютсяbit7/741.0 и границыlow7; участвует в FAN path. Не датчик батареи и не универсальный ток.'),
0x7c8:('Capabilities/status + thermal notification latch','WUJIE bank2 837F','Bit4 — защёлка события при751.7 и44F>=70 сAPP present. EventC8 ставится в очередь. Не battery charging gate. Остальные биты требуют собственных доказательств.'),
0x7d2:('GPU/module selector','Own CC low5 codec + WUJIE firmware','Low5 используется как код GPU/module; не значение TGP. WUJIE dGPU branches могут существовать даже без физической NVIDIA.'),
0x721:('Сырой кандидат GPU-limit','Не расшифровано','Нет direct literal DPTR в WUJIE core; чужой NUC/OEM label не подтверждает назначение.'),
0x72d:('Сырой power context','WUJIE bank1 E86C','Есть consumer в native power logic, конечная единица не установлена. Не выдавать за GPU W.'),
0x72e:('Сырой power context','WUJIE bank1 E87A','Есть consumer в native power logic, конечная единица не установлена. Не выдавать за GPU W.'),
0x44c:('Опубликованный power/context byte','Selected WUJIE code','Есть вычисления и specialFF. Не самостоятельный датчик разряда батареи; масштаб всей функции не закрыт.'),
0x44f:('Кандидат GPU-related thermal input','Own WUJIE thermal consumers','Используется в порогах70/85/80; физическая привязка к GPU не доказана. Ноль нельзя показывать как надёжную температуру dGPU.'),
0x751:('Native performance preset + flags','Own WUJIE firmware','Byte не является целиком одним номером режима:751.7 также участвует в thermal branches. Не зарядный профиль.'),
0x783:('Запрос STAPM','Existing exact-board power contract','CPU-only запрос, не измеренная мощность/SMU readback. Свойствоbattery log не превращает его в флаг батареи.'),
0x784:('Запрос Slow PPT','Existing exact-board power contract','CPU-only запрос, не измеренная мощность/SMU readback.'),
0x785:('Запрос Fast PPT','Existing exact-board power contract','CPU-only запрос, не измеренная мощность/SMU readback.'),
0x726:('Maintenance/control context','Own native age/power paths','Есть service/maintenance bits. Нельзя объявлять свободными батарейными переключателями; перепрошивка/сброс возраста не предлагаются.'),
0x727:('Maintenance/control context','Own native inhibit/power paths','Влияет на несколько ветвей. Не самостоятельный процентный порог или разрешение зарядки.'),
0x7c5:('Смешанный control context, включая подсветку','Exact-board lighting contract','Low3 относятся к Pulse selector в рабочем keyboard protocol; в другом контексте могут быть другие биты. Не весь байт battery state.'),
0x7c6:('RamFan/control mailbox','DSDT WMS0 + own WUJIE fan code','Low2=WMS0;bit2 допускает CPU/GPU RAM fan paths8D66/8F66. Другие биты — отдельные strobes/служебные поля. Не батарейный режим.'),
0x7b3:('PMAX raw','GM6 DSDT/OEM name','Пока без подтверждённого firmware consumer/единиц. Название в ACPI не доказывает реальные доступные W.'),
0x7b5:('PBSS raw','GM6 DSDT/OEM name','Пока без подтверждённого firmware consumer/единиц. Не SOC или верхний порог.'),
0x7b7:('PSRC raw','GM6 DSDT/OEM name','Не перенесён на WUJIE; runtime-codec не установлен.'),
0x7b8:('DTTF raw','GM6 DSDT/OEM name','Не перенесён на WUJIE; runtime-codec не установлен.'),
}
for a in (0x730,0x731,0x732,0x734,0x735,0x736):notes[a]=('Публикуемый native power-table byte','Own WUJIE table publication','Не измерение батареи. Полный source/table-index/unit codec в этом проходе не закрыт.')
for a in (0x7a7,0x7a8,0x7a9):notes[a]=('Публикуемый CODE table byte','Own WUJIE bank2 9BC6..9BF2','Получен из выбранной таблицы, не живой термодатчик. Названиеthermal_target? остаётся гипотезой до полного table codec.')
for a in (0x50a,0x50c,0x50e,0x510):notes[a]=('Внутреннее знаковое расчётное слово тока','GM6 EC1.12 selected raw instructions','Диагностика, не прямой SBS Current. Не назначать знак434 из другой по времени публикации. Масштаб/guards зависят от ветви.')
for a in (0x370,0x372,0x374,0x376):notes[a]=('BE16 внутренний отсчёт','GM6v1.12 samplerD347','Один из четырёх входов фильтра. Физический датчик и ADC units не установлены; не амперы.')
notes[0x399]=('Поправка нуля','GM6v1.12 source trace','Участвует в reference0200+399. Не готовое значение мА.')
notes[0x382]=('BE16 промежуточное расчётное слово','GM6v1.12 EB85 trace','Сырое signed interpretation; не независимое физическое измерение.')
for a in (0x367,0x3a5):notes[a]=('Флаги внутреннего расчёта тока','GM6v1.12 selected paths','Полная расшифровка всех битов не завершена; unsigned/signed сырьё не даёт нового CHARGE_EN.')
notes[0x834]=('Подготовленный ток зарядного контроллера','GM6v1.12 own native buffer','Команда14 использует подготовленный буфер. Не measured Current и не подтверждённый SMBus ACK.')
rows=[]
for group,fields in d['catalogues'].items():
 for f in fields:
  meaning,proof,limit=notes.get(f['address'],('Не установлено','Пока только сырой logger field','Не присваивать имя/единицы по аналогии.'))
  rows.append(dict(f,group=group,meaning=meaning,evidence_kind=proof,limits=limit))
assert len(rows)==103 and all(x['meaning']!='Не установлено'for x in rows)
out=dict(scope='Every field in the two main logger catalogues and GM6 current diagnostics has an explicit semantic or unresolved record. This is full field coverage, not full firmware semantic recovery.',rows=rows,firmware_scope='Exact saved WUJIE core and GM6 EC1.11/1.12; do not transfer calibrated units or private-XRAM access between models.',hardware_execution=False)
(OUT/'field-map.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
lines=['Полный состав регистров батарейного логгера. 26.10.2026','103 описания: WUJIE62, GM6основные27 + CURRENT_CALC14.','Полный охват полей НЕ означает полную расшифровку прошивок. Неизвестное явно отмечено.','']
for r in rows:lines.extend([f"{r['group']} 0x{r['address']:03X} / {r['width']}byte {r['endian']} / {r['logger_label']}",f"  Смысл: {r['meaning']}. Основание: {r['evidence_kind']}.",f"  Граница: {r['limits']}",''])
(OUT/'FIELD-MAP.txt').write_text('\n'.join(lines).rstrip()+'\n',encoding='utf8')
print('semantic/unresolved records',len(rows))
