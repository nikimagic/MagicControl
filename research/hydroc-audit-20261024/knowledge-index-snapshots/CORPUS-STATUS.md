## Актуальные числа PHASE163

186EC+13PEI+9BIOS=208typed;405ассоциаций/117ячеек;23units/5888raw.
[9 новых contexts и1ABI-only](PHASE163-LAST-DIRECT-ANCHORS.md),612 instruction records.
537node213same/285absent/39other;triage213selected/25owner/156legacy/108GX/33other/1owner-other/1ABI-only.
Raw-only=0 не означает полноту: ABI-callers, legacy/GX и прочие косвенные пути открыты.
28P-only helper с CALL-контекстом:126refs/80uniqueCALLsites. Core5/6/7 —23predicatecells.

## Актуальные числа PHASE162

177EC+13PEI+9BIOS=199typed;392ассоциации/117ячеек;23units/5888raw.
[4 branch-consumers](PHASE162-FNLOCK-PLATFORM-ROM-PRIORITY.md),286 instruction records.
537node202same/296absent/39other;triage raw10/owner25/legacy158/GX108.
Pure28/28 имеют CALL-контекст:125refs/79uniqueCALLsites, не полный CFG.
Остальные consumers, точные источники и95/90 остаются открытыми.

## Актуальные числа PHASE161

173EC+13PEI+9BIOS=195typed;363ECассоциации/117ячеек;23units/5888raw.
[9 дополнительных consumers](PHASE161-GM6-P2-DR-P13-CALLERS.md),262 instruction records.
28/28 индексированных pureP-helper имеют CALL-контекст:121refs/75uniqueCALLsites.
Это не полный CFG.537node180same/318absent/39other;triage raw26/owner29/legacy160/GX108.
Оставшиеся consumers, точные источники и95/90 остаются открытыми.

## Актуальные числа PHASE160

164EC+13PEI+9BIOS=186typed;353ECассоциации/117ячеек;23units/5888raw.
[14 input-consumers](PHASE160-P2-P13-INPUT-CALLBACKS.md),375 instruction records,
primary event-header с проверенным blob.28purehelpers,25сlinks;91refs/65uniqueCALLsites.
537node180same/318absent/39other;triage raw28/owner26/legacy160/GX108.
Полные callers, physical inputs, прочие consumers и95/90 остаются открытыми.

## Дополнение PHASE159: wait и предел подтверждения

[Контекст controller-wait](PHASE159-GM6-CONTROLLER-WAIT-BOUNDARY.md) добавляет
652 instruction records, частично повторяющих senders для проверки контекста.
Software pending-clear не доказывает CPU-completion-code. Три data-byte
копируются действительно; полный внешний frame остаётся неизвестным.
150EC/172typed/23units без увеличения, аппаратных действий нет.

## Дополнение PHASE158: назначение native-запросов GM6

[Прослежена очередь мощности](PHASE158-GM6-NATIVE-POWER-REQUESTS.md):
626 instruction records двух точных EC и pinned primary PECI-header.
Это уточнение existingP12-consumers, а не новая EC-ревизия:150EC/172typed/23units.
Software-intent PL1/PL2/PL4 установлен; units, полный frame и ACK остаются открытыми.

## Актуальные числа PHASE157

150EC+13PEI+9BIOS=172typed,334ECассоциации/117ячеек;23units/5888raw.
[GM6caps/RGB](PHASE157-GM6-P12-CAPS-AND-RGB-SELECTOR.md):4consumer,638instructionrecords.
P7RGBдляEC1.11добавляет23-юpredicate-backedячейкуcore5/6/7,непереноситGPUfloorEC1.12.
Pure28helpers,18linked,54links/47unique.537siteиrawcoverageтриажнеизменны.
Физическиеединицы/всеcallers/точныеобразы/95-90не доказаны.

## Актуальные числа PHASE156

146EC+13PEI+9BIOS=168typed,320ECассоциаций/117ячеек;23units/5888raw.
[P12/S12clear7A3.6](PHASE156-P12-S12-CLEAR7A3-POLICIES.md):16literalwriters,
10классифицированныхpredicate-sites,3readerrows/253instructionrecords.
537node180same/318absent/39other;triagerawdirect35/owner18/legacy161/GXsig108.
Pure28helpers,18linked,46links/43unique. Физика/всеcallers/95-90неподтверждены.

## Дополнение PHASE155: контекст десятиcounter-функций

[Entryguards](PHASE155-TEN-EC-COUNTER-ENTRY-GUARDS.md) добавляют558instructionrecords
иexacthelpersвсех10образов.741.0подавляетувеличениеввыбранномпути,нораннийreset
неравенRGB-восстановлению.РазличияIRAMflagsсохранены.23units/136EC/158typedбезувеличения.
Полныйscheduler/clockдляостальных9образовнеперенесёнизMT.

## Актуальные числа PHASE154

136EC+13PEI+9BIOS=158typed,308ECассоциаций/117ячеек;23units/5888raw.
[10counter-policy](PHASE154-TEN-EC-COUNTER-POLICIES.md) подтверждены919instructionrecords.
6directP4variable30/600,RPtestedS4buteffective600,3constant600.
537node:170same/328absent/39other;triage rawdirect43/owner_same18/legacy163/GXsig108.
Pure28helpers,15linked,43links/40uniquephysicalcalls. Остальныеconsumers/точныеобразы/95-90открыты.

## Дополнение PHASE153: границы MT-счётчика

[Entry/reset/scheduler](PHASE153-MT-COUNTER-ENTRY-AND-SCHEDULER.md) добавляет
200instructionrecords и31байтC51data-table ксуществующемуconsumer129.
741.0блокируетувеличениеввыбранномMTпути;условиявозвратаRGBиcoalescingflag
непозволяютвыдать30/600засекунды.130EC/152typed/23units неизменны.

## Актуальные числа PHASE152

130ECпотребителей +13PEI +9BIOS =152typed;301ECассоциация/117ячеек.
[MT P4 counter/GPIO](PHASE152-MT-DIRECT-P4-COUNTER-AND-GPIO.md) добавлен
какconsumer129/130;265новыхinstructionrecords. Триаж rawdirect45/owner_same18,
прочие537siteкатегории прежние.28purehelpers,15сdecodedlinks,35links/32uniquecalls.
23units/5888сырыхстрок не менялись. Физика/полныеcallers/95-90 не доказаны.

## Дополнение PHASE151: внешняя актуальность без раздувания корпуса

[Авторскийupstream иLinuxHEAD](PHASE151-PINNED-UPSTREAM-NAMESPACES.md)
побайтносовпадаютсранееизвестнымsourceblob.Добавленыметаданныеисточников,
не новыеfirmwareunits.23/128EC/150typed остаютсяактуальными.
7CC.7priorityне7C3;точныйmainlineфайлнеимеетSTARTthresholdAPI/enum5-6-7.

## Актуальные числа PHASE150

128ECпотребителей +13PEIинициализаций +9BIOSпотребителей =150typed.
23единицы/5888строк,299ECассоциаций,117связанныхячеек;сырые данные не менялись.
[Три новых S4-policy](PHASE150-S4-OUTPUT1807-POLICY.md) для1807 подтверждены
261instructionrecords. PureP-only28срезов/29knownCALL,неполныйCFG.
Цельне завершена;прежние исторические счётчики ниже относятся к своим этапам.

## Дополнение PHASE149: числовой ROM-потребитель прослежен

[Данные и границы](PHASE149-GX-ROM-DEPENDENT-SIGNED-PUBLICATION.md):
496instructionrecords/дваGX,0382/83 BE,~N и условная публикация050A–0511LE
по497.0, далее выбранный0434/35-reader.40/45/50 не назначеныSOCилиамперами.
23units/147typed без изменения; аппаратных и продуктовых изменений нет.

## Дополнение PHASE148: дваGX selector раскрыты

[ROM/SKU и073C](PHASE148-GX-ROM-SKU-AND-RGB-GATE.md):338instructionrecords,
SKU-helper и полная выбранная функция, RGB-gate и числовой consumer491.
ROM4 не имеет приоритета над положительным SKU12h/17h; этиSKU не являютсяP18/P23.
073C=1 возникает и безROM4. Точные дваSHA, аппаратных изменений нет.

## Дополнение PHASE147: ROM0

[Отдельный ROM0-каталог](PHASE147-ROM0-NAMESPACE-AND-GX-SELECTOR.md) содержит57
прямыхread-якорей десятиmainEC. Фиксированные/первыеарифметическиеROM-константы
04/08/09/0C/18/FF не добавляются автоматически в enum7C3.
GX R4 локально пишет073C=1 при достиженииsuffix; outerguards/073C-purpose открыты.
934новыхinstructionrecords сверены сисходнымиSHA;125EC/13PEI/9BIOS без изменения.

# Статус корпуса — 24.10.2026, PHASE146

**Найденная очередь источников включена; семантика ещё разобрана частично.**

Текущая матрица содержит 23 единицы: 10 EC и 13 BIOS-контейнеров.
В ней разобраны 125 выбранных EC-потребителей и 13 записей BIOS-инициализации.
Это не все доступные локальные файлы и не полные графы вызовов.

## Найденные пропуски

- GXxHXxx N114MRO50: два разных EC и один BIOS уже включены; их политики и датчики ещё разобраны частично.
- Владельческий MRO10-архив: три разных BIOS-контейнера включены; их EC 1.12 уже известен. Совпадение PEI/DEPEX не приравнивает остальные модули и SPI.

В известной очереди теперь **0 образов, ожидающих включения**. Полные SHA и статусы:
[очередь](pending-local-sources.json), [разбор находки](PHASE122-LOCAL-CORPUS-GAPS-AND-GX-PAYLOAD.md),
[включение GX и различия лимитов](PHASE123-GX-NATIVE-LIMITS-AND-CORPUS-EXPANSION.md),
[MRO10 и границы переноса](PHASE124-MRO10-BIOS-VARIANTS-AND-IDENTITY-TRANSFER.md).
Все54 текущие индексированные точки сравнений5/6/7 связаны с локальным поведением; косвенные обращения и полные callers остаются открытыми.

Первый GX EC полностью совпадает по SHA с `GXxHXxx_21.200` из сохранённого
первичного китайского анализа. Утверждение прежних фаз, что его бинарник
отсутствует локально, исправлено. Точные Wujie 15X EC1.18 и L16 MAC10 этим
не получены: одинаковое семейство производителя не означает одинаковую модель.

## Границы inventory

Проверены три указанных корня по именам, размерам и SHA; известные 23 пути
добавлены явно. 115 файлов-кандидатов сведены в 69 групп. Среди них есть части
установщиков, фрагменты распаковки и отдельный Honor — это не новые ноутбуки.
Безымянные файлы и содержимое всех архивов таким фильтром не исключены.

UEFIExtract использован только как закреплённый статический parser.
OEM-пакеты, EC-код, EFI и драйверы не запускались. Продукт и аппаратные
настройки не менялись. Температурные доказательства старых 8 EC не перенесены
на два GX без собственных доказательств. Для батареи GX теперь есть отдельный разбор; Для CPU/GPU GX добавлены выбранные цепочки запросов; полное покрытие остаётся открытым. Успешный audit подтверждает целостность записей и их scope,
**не завершённость исследования**.

[Новый разбор S5: CODE-таблицы и условные параметры](PHASE125-GX-S5-CODE-TABLES-AND-PARAMETERS.md). Девять anchors связаны с локальными действиями; полный граф не закрыт.

[GX: входные маршруты S6/S7 и callback вентилятора](PHASE126-GX-INPUT-S6-S7-AND-FANBOOST.md). Проверены оба образа; один S5-префикс имеет только локальную семантику.

[GX: парное восстановление верхнего порога и RGB](PHASE127-GX-UPPER-RESTORE-COPIES-AND-RGB.md). Пара ответов сравнивается на равенство; независимого нижнего порога на этом пути нет.

[GX S5: три входных callback и два выбора таблиц](PHASE128-GX-S5-CALLBACKS-AND-STRIDE9-TABLES.md). Последние два приоритетных кандидата — S7 в34c; остальная семантика и GX-датчики также остаются открытыми.

[GX S7: параметры и ветка положительного счётчика](PHASE129-GX-S7-PARAMETERS-AND-POSITIVE-TIMER.md). Индекс5/6/7 не равен полному графу; GX-датчики и остальные значения ещё требуют разбора.

[GX: источники температуры батареи](PHASE130-GX-BATTERY-TEMPERATURE-SOURCES.md): две собственные цепочки ADC/command08/публикации. CPU/GPU GX, RAM/диски и полные callers не закрыты.

[GX CPU/GPU и S29](PHASE131-GX-CPU-GPU-QUERIES-AND-S29.md): добавлены запросы/фильтры/подстановки и отдельный потребитель платформы29. Остальные источники, физические датчики и полные callers не сертифицированы.

[Покрытие всех26 индексированных кодов](ENUM-VALUE-COVERAGE.md):19 имеют нормализованный predicate-разбор хотя бы в одном EC;Все114 пары текущего EC/код-индекса связаны хотя бы с одним локальным разбором. [Причины и адреса](PHASE132-ENUM-COVERAGE-AND-P1-P14-P16.md).

[GM6 EC1.11/1.12: P9/P14/P18/P26](PHASE133-GM6-P9-P14-P18-P26.md): добавлены шесть условных потребителей; слова/указатели/dispatch различены.

[0A/0F/20/FF в EC](PHASE134-P15-SFF-AND-RGB10-32.md):провереныещёшестьлокальныхпотребителей. Увсех19EC-кодовсравненияестьхотябыодинразбор;семьостальныхкодовнайденывBIOS. Это непокрываетвсеsites/образы.

[BIOS: шесть потребителей графических ресурсов](PHASE135-BIOS-IMAGE-PLATFORM-CASES.md) добавлены отдельно отEC/PEI. Теперь26из26кодовимеютхотябыодинлокальныйECилиBIOSразбор;прежниемежобразныепробелыостаются.

[X6DR P31: изображение и условный Shutdown](PHASE136-X6DR-P31-IMAGE-AND-SHUTDOWN.md):девятьBIOSconsumersвчетырёхPE-модулях,отдельноот97ECи13PEIinitializers.

[Дополнительные RGB-предикаты](PHASE137-RGB-PREDICATES-CROSS-IMAGES.md):четыреобразыважноразличатьпоP-only/P-or-ROM. РазобранныепарыEC/код90из114;24ещёоткрыты.

[Код10 и GX S2](PHASE138-P10-KEYBOARD-TAILS-AND-GX-S2.md):реальныеP10подстановкиGM6/HGотделеныотлокальнонеиспользованногоS10вGX. Разобранныеrawпары97из114,17ещёоткрыты.

[ПорядокGX S12/S14/S16](PHASE139-GX-S12-S14-S16-ORDER.md):показанпрямойвыходS12передпозднимиremap,13входныхподстановок. Разобранныеrawпары103из114;11ещёоткрыты.

[AR5 P18/P26 и GX S29](PHASE140-AR5-P18-P26-GX-S29.md):трипарыполучилилокальныйразбор;сырыепараметрыневыдаютсязагарантированныеватты. Разобранныеrawпары106из114;8ещёоткрыты.

[RP/DR код2, RP16, AR4513](PHASE141-RP-DR-P2-S16-AR45-P13.md):четырепарыполучилилокальноеописание;rawпокрытие110из114,осталисьчетырекод8.

[S8 в четырёх образах](PHASE142-S8-REMAPS-AND-TABLES.md):rawпокрытие114из114пар. Это неполныйper-site/CFGаудит;полнотаисходнойцелипроверяетсяотдельно.

[Аудит исходной цели](GOAL-REQUIREMENTS-AUDIT.md) обнаружил болееузкийхарактерпарногопокрытия:537sites,160same-valuewindows,40other-value,337безselectedinstructionwindow. Это не337доказанныхновыхдефектов. [Сводка5/6/7](PLATFORMS-5-6-7.md) и [исправлениеpositivechargeevidence](PHASE143-GOAL-AUDIT-AND-POSITIVE-CHARGE-EVIDENCE.md). Полное завершение не доказано.

[Триаж и новыеS4/S12веткиXxRP](PHASE144-SITE-TRIAGE-AND-RP-S4-S12.md):parent-SHA/архивныеprefixотделеныотselected-node,двефункциипрослеженыдоRETссоставнымизаписями1602/1605/1603. Полнота цели всё ещё не доказана.

[Независимыйheader-sweep](PHASE145-HEADER-SWEEP-AND-MRO10-PD.md):315333пути/1632заголовка,20SHAгруппклассифицированы;новыхmainECревизийвэтихсовпаденияхнет. ВMRO10учтёнотдельныйPDpayload4f5918…;егоruntimeсемантиканеразобрана. Границыфильтраявные,матрица23единицсохранена.

[Вынесенныепредикаты](PHASE146-OUTLINED-PREDICATE-CONTRACTS.md):28P-onlyXOR/RETсрезов,26известныхdecodedCALLсвязейдля14изних;зарядныедоказательствав8образахтеперьсодержат14разрешённыхcalleeсвязей. Количествопрофилей/кодови125purposeзаписейнеизменялось.
