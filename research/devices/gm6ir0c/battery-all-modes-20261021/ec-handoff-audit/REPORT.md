# EC1.12: от BMS/Healthy/Reached к реальному SMBus charger request

22.10.2026. Только статический разбор файлов. Новый EC image SHA-256
`5659c8217e9d93d3fd6fe278756872d7c5adbd15a0687d35705e2df4bb5c05dd`.
Продукт, BIOS/EC/OEM код не запускались; live EC/UEFI/SMBus не читались и не
изменялись. Production/test/build продукта не менялись.

## 1. Область и доказательства

Новый source соответствует family/Project25/ECversion1.12 и сохранённым live
state fields. Полный live flash hash не получен: см.
[identity addendum](../bios-audit/MRO10-EC112-ADDENDUM.md).

Основной артефакт [evidence.json](evidence.json): исходный SHA, bank/logical/
physical offsets, raw bytes/hashes, сравнение выбранных блоков с1.11,17 direct
platform read sites,13 helper callsites и реальные CODE table bytes.
`collect.py` читает только saved images/архивные dumps.

Ghidra exports рядом содержат одновременно C и RAW INSTRUCTIONS. **8051 C
prototypes, carry и register arguments не считать надёжным C-контрактом.**
Ниже выводы опираются на RAW/байты. Некоторые exploratory platform entries
начинались внутри блока или проходили C51 inline switch data, что отмечено
warning в output; их псевдо-C не используется как доказательство поведения.

Bank mapping: common0000–7FFF неизменен; bank0 window8000–FFFF соответствует
physical8000–FFFF, bank1 —10000–17FFF, bank2 —18000–1FFFF. Это явный выбор
CODE window, не старый ошибочный bank*10000 mapping из сторонних комментариев.

## 2. Target, outgoing buffer и измерение — разные величины

|Слой|Поля|Endian/смысл|
|---|---|---|
|BMS request|030E/030F,031A/031B|BE voltage/current request|
|Расчётный target EC|0522/0523,0520/0521|LE voltage/current budget после политик|
|Outgoing charger words|0836/0837,0834/0835|LE voltage/current, реально выбираемые writer|
|SMBus staging|00C0/00C1|Low/high байты отправляемого word|
|Измерение|0434/0435,0438/0439|Actual battery current/voltage из телеметрии, отдельный слой|

Bank1 DCBA..DCDD копирует BMS BE значения в target LE без масштабирующего
преобразования. Healthy/aging изменяют target voltage в другом consumer,
разобранном root; эта часть не подменяется интерпретацией имён host UI.

Сохранённый live dump175302 с valid mask даёт:

- 030E_BE=13200,031A_BE=5000;
- 0522_LE=12600,0520_LE=5000;
- 0491&C0=80 —3cells по найденному EC cell-count coding;
- 0454=4C,740=19,7C3=7,770=FF.

Следовательно разница13200→12600 соответствует **3×200mV в nominal charging
voltage request**, а не200% или200 единиц SOC. Точные aging thresholds/root
calendar/counter branches сохраняются в root ec-audit отчёте.

0520 может оставаться5000, когда stop path уже назначает0834/35=0. Поэтому
один target0520 не доказывает положительный фактический ток или проигнорированную
остановку. Важны outgoing buffer, результат SMBus и измеренный ток.

## 3. Полная handoff цепочка voltage/current

### Voltage

Bank1 DE0F:

1. 0522→0836 (low),0523→0837 (high).
2. DE6B устанавливает **0832.bit1**, вызывает common1C06.
3. Common1C06 raw `90 C9 29 02 11 00`: DPTR=C929, dispatchbank0.

### Current

DE7F иC086 — два пути установки **0832.bit0**.

- DE7F завершает запись current high-byte в0835 и вызывает1C06.
- B35F публикует рассчитанный R6:R7 current в0834/35 и черезC086 ставитpending.
- Stop path B37C пишет08E4=2,0834=0,0835=0 и черезC086 ставитpending.
  Это прямое продолжение Reached consumer, найденного отдельным агентом:
  C14F→B7AB→B3C6/B3BC→B37C. Здесь Reached reader заново не восстанавливался.

### Pending → charger command

Bank0 C929 приpending:

|Pending|Источник low/high|Command|
|---|---|---:|
|0832.bit1|0836/0837|15h|
|0832.bit0|0834/0835|14h|
|0832.bit2|0838/0839|3Bh|
|0832.bit3|083A/083B|3Fh|
|0832.bit5|0830/0831|3Dh|

Выбранный pending bit снимается, low→00C0, high черезCFAE→00C1. CFAE оставляет
DPTR=0A52. CF84 создаёт request:0A52=command,0A53/54=pointer00C0,0A55=0.
Общий хвостC9BE задаёт **R3=12h,R5=8Ch,R7=0** иtail-jump8809.

### Реальный SMBus hardware backend

8809 сохраняет R7→0A4F(channel),R5→0A50(protocol/flags),R3→0A51(slave).
Для8Ch исполняется write branch; после снятияbit7 protocol0Ch выбирает
два data bytes. Helpers формируют XDATA hardware register addresses:

- 8DE1:30DCh+channel×15h, куда пишется slave12h без shift;
- 8E0A:30DEh+channel×15h, command14h/15h;
- 8DFA/8E36:30E2h/30E4h+channel×15h, два data bytes;
- 8E77 возвращает pointer00C0 из0A53/54, затем backend берёт low/high.

Это **не просто запись отображаемой RAM-телеметрии**: firmware доходит до
SMBus master registers, запуска/ожидания completion/error flags. Путь содержит
timeout/error обработку; конечный ACK, принятый DAC и реальное прекращение тока
в данном исследовании не измерялись.

12h —8-bit wire address, то есть7-bit09h. Команды14/15 и их units определены
стандартом Smart Battery Charger: current вmA, voltage вmV, WriteWord; нулевая
команда завершает заряд. Это semantics интерфейса, не найденная маркировка
конкретного кремния. [SBS Charger Specification1.1, §§5.1.1–5.1.2](https://www.sbs-forum.org/specs/sbc110.pdf).

TI bq24780S имеет совместимые address12h/commands14h/15h и дополнительные
3Bh/3Dh/3Fh, но **настоящий IC/ManufacturerID/DeviceID GM6 не установлен**.
Не переносить его DAC шаг/точность/прочие bits как измеренную спецификацию
этого ноутбука. [TI datasheet](https://www.ti.com/lit/ds/symlink/bq24780s.pdf).

## 4. Pending ноль обслуживается и без DE7F

B37C/C086 сами не вызывают1C06. Независимый обычный consumer найден:

- common1C60→bank0C89A;
- C89A при state0681=0 обходитstep0680;
- **step2 → LCALLC929 atC8E5**, затем0680=3;
- следующий step возвращает0680=0.

Внешний scheduler edge найден отдельным агентом:
common0E88→156A→1570→bank2:85F4→tail1C60. Его точная cadence/conditions
сохраняется в ec-percent-audit. Здесь достаточно raw edge, чтобы pending zero
не оставался «только установленным флагом без найденного consumer».

Условный normal resume тоже имеет конкретный writer: B5FC budget paths→B756
(сохранение current intent)→B661, затем допустимый tailB71D→B32D(voltage),
C186(readR6:R7 from0A5C/5D),B35F(current),C086(pending). Другие safety/power/
temperature gates могут выбрать stop. Это не обещание unconditional resume
при снятииReached и не найденная роль lower7D0.

### Особенность DE0F: промежуточный ненулевой current

При outgoing current0 DE0F ставитprivate0A4E.bit1, временно пишет current
**0400h**, вызывает writer, выдерживает delay с аргументомFAh, затем в этой же
функции поbit1 снова пишетcurrent0 черезDE7F. Local final command остаётсяzero.
Аргумент задержки не назван миллисекундами без определения clock/Delay API.
Не объявлять stop абсолютно монотонным при любом порядке voltage refresh;
глобальная очередность и физическая transient реакция отдельно не измерены.

## 5. Ограниченный inventory побочных действий platform7C3

### Не путать два источника команды12600mV

В bank1 B338/B2DE действительно встречается literal3138h=12600. Его
непосредственный selector — common1936→bank2 C102→DC26, который читает
**0497.bit0 ISDB**, не7C3: приISDB1 берётся target0522/23, приISDB0 B338
задаёт0836/37=3138. Это фиксированная CV fallback команда12600mV,
численно3×4200mV для3S; совпадение числа не доказывает вызовplatform4.
В рассмотренном live snapshotISDB=45(bit0=1), поэтому наблюдаемые
030E13200→052212600 согласованы сaging, а не сэтимISDB0 fallback.
Отдельный causal path7C3→изменениеISDB здесь не найден; его не предполагаем.

В1.12 найдено **17 literal `MOV DPTR,07C3`** (включая generic getterB200).
Helperbank2 D827 возвращает`7C3 XOR4`, имеет13 прямых same-bank LCALL sites.
Все смещения/байты включены вevidence.json. Это не17 разных эффектов4vs7:
многие direct comparisons проверяютдругиеplatform IDs и для4/7 одинаковы.

### Подтверждённые условные ветви4vs7

|Ветка|Доказанный эффект|Ограничение|
|---|---|---|
|bank2:880B|4→CODE pointer-group5D78, иначе5D90 в08E6/E7|Другая таблица выбора factory FAN curves, зависит от внешнего SKU switch|
|bank2:883F|4→5E08, иначе5DF0|Вторая группа FAN pointers, не соседний generic5DD8|
|bank2:9A0B/9A1E/9A65/9A84|При CCe7 guard nonzero 4 выбирает PL-table6149, другие6178|Иные board/CPU guards могут датьоднутаблицу ипри4и7|
|bank2:8FA9/92C7/C7A6|Thermal/fan decisions с043E/044F,0460/0468,0670/1809,0876/087E|Не восстанавливалась вся PWM функция/все floors|
|bank2:B571/B585|Private08A5/08A6 counter limits различаются30/600|Единицы и полное назначение counter не доказаны|
|bank0:B918|Platform4 — отдельное условие допуска Fn-lock action;для7 требуется другой допуск|BB29 XOR74E.4 иeventB8; это Fn-lock, не доказанная смена режима backlight|
|bank2:CECD|Percentage feature gate4 OR ROMID770=4|Разобран ec-percent агентом; consumer не дублируется|

**FAN bytes:** group5D78 first pair→5598/5B68, group5D90→55F8/5BC8;
group5E08→5418/59E8, group5DF0→53E8/59B8. Это48байт структуры Up16/Down16/
Duty16 с rawDuty0..200, а не просто подписи интерфейса. НапримерCPU5598
Up54,59,61,…,75,FF… иDuty0,60,60,70,…,200; реальные массивы сохранены вJSON.

**PL bytes:**6149 leading tuple75/75/200/0,6178 leadingtuple75/75/100/0.
Поздний код переносит выбранные данные в0730/731/732,7A7/7A8/7A9 идругие
native default fields. Это **raw requests/defaults**, не гарантия ватт/масштаба
PL4/реального CPU enforcement. Нельзя выдавать условную ветку за измеренное
изменение на данном live состоянии.

Прямые platform comparators не4/7: bank0B766/B784/BAC0/BB07/BB22/C179,
bank2:96B5/96CC/9743/DD86/DDF1/DDFF/DE74. Дляпрямо проверяемогоpredicate
оба4и7 обычно даютодин результат;generic getterB200 требует дополнительных
callsite checks. Отдельный explicit7 branch8FFE входит вthermal/fan path.
Прямого доказанного «platform4 меняетbacklight brightness» здесь не найдено;
это не доказательство отсутствия всех косвенных keyboard effects.

## 6. Практический вывод из кода

7C3 — **глобальный platform selector, не battery-only enable**. Переназначать
4 вместо7 ради процентов означает вмешиваться в общие FAN/PL/thermal/keyboard
ветви. ROMID770 тоже участвует в SKU/policy dispatch, а не является свободным
зарядным toggle. Исследование не даёт основания писать любой из них автоматически.

Сейчас замкнута статическая цепочка **policy/Reached → outgoing target →
pending queue → SMBus current/voltage command**, с units по первичной
спецификации. Не замкнуто аппаратное доказательство ACK/физического stop,
точной задержки, конкретного IC, полной живойflash и всех побочных переходов.
Никакого переноса чужих Wujie наблюдений в эту карту не делалось.
