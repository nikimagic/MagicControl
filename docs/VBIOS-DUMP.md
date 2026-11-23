# VBIOS и полный EEPROM NVIDIA

## Два явных режима

- **Снять дамп VBIOS** / `mctl dump-vbios`: только два обычных чтения,
  проверка структуры и совпадения. При `vbios_saved=true` операция успешна,
  `--check` не вызывается.
- **Снять полный EEPROM** / `mctl dump-eeprom`: проверка геометрии и два
  чтения основного EEPROM при допуске. Неизвестная ёмкость даёт статус
  «Полный EEPROM не прочитан: ёмкость чипа не определена».

В INFO обеих ОС выбор находится в существующей кнопке NVIDIA, без привязки
к конкретной модели. CLI возвращает 0 за подтверждённый запрошенный образ
или 4 при отказе, отмене или ошибке. Ниже — история прежних сборок и проверки.

Автономная ревизия: проверяются раздельные последовательности команд,
диалоги результата, гибридный Intel/NVIDIA, отмена, остатки драйвера и
привязка PCI. Пустой файл останавливает повторное чтение; полный EEPROM
из одних 00 или FF отклоняется даже при совпадении размера и хэшей.
Перед проверкой ёмкости после raw compare повторяется привязка PCI и
индекса NVFlash. Сохранённый ROM GM6 проверяется по хэшу и разбирается
офлайн для правильного и чужого Device ID.

## Уточнение по инструкциям и частичному результату,27.10.2026

OEM Fastwel VIM556 manual(section5.1) и опубликованные backup-процедуры
подтверждают самостоятельный --save без обязательного предварительного --check.
Это подтверждение порядка обычного backup, не совместимости с GM6. Два чтения,
SHA и структура — дополнительные проверки нашего collector. Актуальная полная
публичная документация NVIDIA5.867 не найдена; семантика entire проверена по
точному собственному payload/встроенной справке. Ссылки и границы:
research/nvflash-instructions-recheck-20261027/REPORT.txt.

Общий VbiosRomDumpResult.outcome различает completed/completed_partial/failed/
cancelled. GUI обеих ОС сохраняет typed результат вместо потери двух признаков
при копировании в DiagnosticDumpResult. Audit GUI и CLI отдельно пишут
vbios_saved/eeprom_saved. saved по-прежнему означает оба подтверждённых режима,
CLI exit0/4 сохранён. Если ни один не подтверждён, manifest failed, не partial.
Окно сразу показывает mode_failed/entire_refused и отдельно сохранённый VBIOS.
Размеры окон/скролл/команды NVFlash не меняются. Это не доказательство причины
обрыва0.1.210 или успешного чтения на GM6: на железе помощник не запускал.

## GM6 archive19: отдельное обычное чтение,27.10.2026

В0.1.210 после --check exit0 с Restricted or unsupported журнал и сессия
оборваны на eeprom_capacity; ни save, ни итоговой ошибки нет. Точная причина
аварийного/внешнего завершения по архиву не установлена. Не объявлять её
доказанной ошибкой конкретного драйвера или успешным чтением.

Обычный VBIOS теперь выполняет два чтения/проверку до необязательной проверки
ёмкости полного EEPROM. Restricted/unknown geometry запрещает только entire,
не отменяет проверенный обычный результат. Стадии mode_started/capacity_probe
и terminal capture_finished/capture_failed пишутся явно. C++ исключения
неизвестного типа превращаются в failure result; ошибка сохранения итоговых
метаданных отдельно остаётся в основном логе. Это не защита от завершения
процесса ОС, access violation или потери питания.

Повторный rebind читает свежий OS physical devnode и NVFlash list с полными
IDs и BDF. Только отсутствующий segment дополняется исходным NVML identity,
без повторного полного NVML power/thermal опроса внутри ROM capture.
Неоднозначность/смена физического узла или index по-прежнему прерывают сбор.
Allowlist List/Check/Save/SaveEntire/Compare, проверка ёмкости entire, два SHA
и raw compare, leases и cleanup сохранены. Команд записи firmware нет.
Нового аппаратного запуска не было; проверка ограничена исходниками и сборкой.

## Ревизия перед выпуском0.1.210,25.10.2026

На синтетике воспроизведены два отказа209 и исправлены:

- Windows PnP каждый раз может возвращать B/D/F без сегмента. Теперь одна
  bindProviderPci дополняет адрес как при выборе, так и при каждом rebind
  перед/после чтения. Нет domain0/index0/display fallback. Гибридный Intel,
  выводящий изображение и стоящий первым в OS enumeration, не выбирается
  вместо NVIDIA. Неоднозначный/недоступный NVIDIA PCI останавливает сбор.
- PCI3 device list проверяется и при прямом совпадении DeviceID: указатель
  должен быть после структуры PCIR, список обязан завершаться0 в пределах
  image. Попадание в собственный PCIR или найденный ID без терминатора не
  считается проверенным образом. Исходные ROM bytes не исправляются.

Удаление остановленной регистрации допускает общий чистый ResidueProof gate,
используемый самим Windows adapter: SDK-значения подтверждаются static_assert.
Гейт/порядок cleanup/original-error precedence и transcript проверяются без SCM.
Офлайн collector покрывает18 сценариев, включая hybrid Intel/NVIDIA, отсутствие
NVML identity, смену PCI, несовпадение NVFlash list, отмену до/после первого чтения
и ошибку post-reader cleanup. Нативные adapters/NVML/vendor/драйверы в тесты
не входят. Реального удачного NVIDIA ROM fixture у нас пока нет; тесты не
подменяют проверку NVFlash на ноутбуке и не доказывают bootability.

## Остановленная регистрация NVFlash,25.10.2026

После каждого завершённого Windows NVFlash command тот же guard повторяется:
успешный/ненулевой exit, исключение и отмена. Проверенная остановленная запись
удаляется сразу, а не только перед следующим дампом. Удаление регистрации не
равно остановке активного драйвера. При pending/live процессе очистка откладывается,
исходная ошибка и lease сохраняются; никакого kill/StopService. Причина очистки
и отказа видна в общем окне и полном логе. `driver_cleanup_checked` фиксирует
проверку даже когда vendor уже самостоятельно удалил службу.

Окно операции показывает проверенные этапы: подготовка, выбранная видеокарта,
обнаруженная/уже остановленная служба, проверка остатка, запрос/результат удаления
регистрации, фактический vendor_started для чтения, сохранённые проходы,
совпадение/проверка образов. Повторные preflight не размножают одинаковые строки.
Фраза «служба уже остановлена» — наблюдение; приложение её не останавливало.
Ошибки красные, проверенные результаты зелёные; raw/пути/хэши остаются в файлах.

Архив GM6(18),0.1.206: preflight встретил nvflash,state1(STOPPED),
start3(DEMAND),image System32/drivers/nvflash.sys. Reader не запускался.
В закреплённом NVFlash CreateServiceA при ошибке завершается исключением;
ветка повторного открытия существующей службы здесь не найдена. Поэтому
остановленная регистрация тоже мешает новому чтению.

При следующем явном запросе VBIOS GUI/CLI могут удалить только такую
проверенную неактивную регистрацию: точное разрешённое имя, kernel/demand,
канонический System32/drivers или текущий Temp, ожидаемое имя SYS, отсутствие
процесса NVFlash. Существующий SYS должен совпадать с одним из двух закреплённых
подписанных reference; ссылки/переадресации/неизвестные файлы запрещены.
Если SYS уже отсутствует, допускается удаление лишь записи с тем же строгим
именем/типом/путём. Перед DeleteService повторно проверяются состояние,
конфигурация и процессы; ожидание удаления ограничено3секундами.

Работающие/переходные службы и процессы не останавливаются. SYS не удаляется
и не перезаписывается нашим кодом: проверенный остаточный файл допускается
к штатному lifecycle самого NVFlash. Неизвестный SYS блокирует чтение.
Проверки/идентичность/результат удаления пишутся в events/main log. Эти изменения
не разрешают прошивальные команды: прежние list/check/save/entire/compare сохранены.
Агент ничего из этого на железе не запускал; фактический повтор нужен владельцу.

## Windows console context,25.10.2026

GM6 archive17: PCI/index binding и--list успешны, но--check печатает
`PROGRAMMING ERROR: Cannot find console window`; последующие запуски видят
service nvflash state4. Runner раньше применял CREATE_NO_WINDOW ко всем tools.
Теперь **только NVFlash** получает CREATE_NEW_CONSOLE со STARTF_USESHOWWINDOW/
SW_HIDE: реальная отдельная консоль существует, но первоначально скрыта.
Parent GUI/CLI/SSH не вызывает AllocConsole/AttachConsole. Direct stdout/stderr,
NUL stdin, наследуемая lease и безопасное ожидание процесса сохранены.
AFU/FPT используют прежний CREATE_NO_WINDOW и свои прежние working directories.

Флаги/контекст пишутся в vendor_execution_context. Console-error контролируется
и в CLI без окна сообщений; читается rolling tail уже существующих raw-файлов.
При её повторении операция даёт явную ошибку, не ждёт10минут молча. Живой
reader не убивается, pending waiter сохраняет lease до его завершения.
Службы не останавливаются/не удаляются: collision включает state/image/start
type и отказ. Отдельно различаются stopped registration и активный driver.
Перезагрузка убирает живой процесс/загруженный драйвер, но удаление оставшейся
регистрации после неё не обещается: её состояние проверяется новым preflight.

**Разрешены только прежние list/check/save/entire-save/compare.** Прошивальных
команд, снятия защиты, reset/disable-driver и правок NVFlash binary нет.
Никаких firmware writes не добавлено; ROM-файлы сохраняются на диск.

Семантика Windows API подтверждена по
[Process Creation Flags](https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags)
и [STARTUPINFOW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow).
Это изменение запуска читалки, не подтверждение аппаратного результата без
нового пользовательского запуска.

Версия0.1.196: одна INFO кнопка и mctl dump-vbios сохраняют **оба результата:
обычный VBIOS и основной EEPROM через --entire, по два чтения каждого**.
Ёмкость определяется из распознанного --check descriptor, не из размера ROM.
При отказе одного режима результат другого сохраняется как partial.
[Декомпил и критерии полноты](../research/nvflash-save-scope-20261025/REPORT.md).

Один collector `createVbiosRomDump` используется для Windows/Linux GUI и
консоли. Это отдельная явная операция, без EC, ROM-ID, FAN/APU restore,
команд программирования flash, protectoff, reset или наших действий по
отключению display driver/выгрузке модулей. Утилита NVFlash5.867.0 является
proprietary vendor reader; её внутренний доступ к GPU/временному драйверу
не равен пассивному чтению файла. На железе помощником не запускалась.

Уточнение0.1.195: Linux child создаёт отдельную сессию без управляющего TTY;
терминальные SIGINT/HUP от SSH не прерывают его текущее чтение. Child обеих
ОС наследует capture lease, удерживая её даже при завершении frontend.
Pending waiter сохраняет также DeviceOperation gate до завершения reader.
Явные внешние сигналы конкретному PID и сбой ОС этим не перехватываются.

## Доступность и выбор

Кнопка INFO доступна только при фактической NVIDIA из OS inventory и для
текущего компьютера. Чужой профиль в меню не открывает доступ; Unknown
разрешает только обнаруженное железо. При нескольких NVIDIA GUI предлагает
выбрать PCI-адрес, CLI требует адрес явно. Одна GPU выбирается автоматически.
AMD/Intel iGPU не подставляются вместо NVIDIA.

GPU NVIDIA вкладка существует у14 известных dGPU-платформ, даже если карта
временно выключена/без драйвера, и для фактически обнаруженной NVIDIA на
Unknown/другом компьютере. Чужой preview показывает только GPU его каталога.
Безусловные пустые GPU-вкладки Unknown убраны. Radeon вкладка требует реально
обнаруженную дискретную Radeon; встроенная графика её не включает.

CLI (права берутся из текущего сеанса; UAC/sudo/polkit автоматически не вызываются):

```text
mctl dump-vbios
mctl dump-vbios 0000:01:00.0 --output D:\captures
mctl dump-eeprom 0000:01:00.0 --output D:\captures
```

Windows требует elevated session, Linux — root. Exit 0 означает успех
запрошенного режима, exit 4 — отказ, отмену или ошибку.
CLI JSON отдельно содержит vbios_saved/eeprom_saved/capacity_verified/eeprom_bytes.
Команда выполняется до startup ROM-ID чтения и не создаёт EC controller.

## Последовательность

1. Новая уникальная папка `logs_<плата>/vbios-dumps/vbios-<UTC>-N` либо
   `<--output>/vbios-dumps/...`; durable events/manifest создаются до precheck.
2. Полный OS список NVIDIA, physical ID, BDF, VEN/DEV/SUBVEN/SUBDEV, driver;
   отдельные строки NVML name/UUID/PCI/current VBIOS/status как evidence.
3. Межпроцессная lease, проверка прав/runtime/остаточных driver/service файлов.
4. Фиксированный NVFlash `--list`. Сопоставление индекса только по всем четырём
   PCI IDs и S/B/D/F. Нет index0/name/monitor-route fallback. Неизвестный формат
   ответа или неоднозначность означает отказ с сырым выводом.
5. Сначала выполняются обычные --save pass1.rom и --save pass2.rom,
   проверяются размеры/SHA и структура. Отдельный --check не является
   обязательным условием ordinary backup и не проверяет checksum ROM.
   Каждое чтение предваряется повторной OS/--list binding.
6. Для полного EEPROM --index=N --check должен дать известные EEPROM ID/part
   и uncapped геометрию Kxwidth. Ёмкость=K*width*128байт; например16384Kx1
   или2048Kx8 дают2МиБ. ID/ёмкость повторяются перед обоими entire чтениями
   и после compare. Unknown/all-zero/all-ones ID, противоречия, parse overflow,
   размеры>16МиБ и marker Limited the part size полный дамп не допускают.
7. При допуске выполняются --entire --save eeprom-pass1.rom и eeprom-pass2.rom.
   Каждый read предваряется повторной OS/--list binding; index0 не предполагается.
8. Фактические размеры и SHA256 считаются до проверки формы, включая ошибки.
   Нужны непустые равные размеры/SHA обоих проходов. Смена GPU/index — отказ.
9. Для обычного VBIOS проверяются55AA, PCIR chain/границы/длины/тип legacy или EFI, NVIDIA vendor
   и device (включая PCI3 device list), legacy checksum. EFI сумма записывается
   отдельно, без подмены её legacy checksum. Канонический `vbios.rom` сверяется
   с хэшем проходов ещё раз. Entire-файлы обязаны иметь ровно установленную
   ёмкость чипа и одинаковые SHA. Далее --compare должен вернуть exit0,
   отсутствие ошибок и положительный Firmware images are identical.
   `eeprom-full.rom` сверяется по SHA ещё раз. PCI структура full EEPROM
   записывается как наблюдение: полный чип может содержать испорченную прошивку.

`saved=true` означает оба результата. Manifest schema2 разделяет vbios/eeprom,
хэши/состояния/причины отказа, ёмкость и её источник. physical_spi_complete=true
требует всех entire-проверок и относится только к выбранному основному EEPROM;
это не все микросхемы платы и не гарантия загрузки/восстановления. Ёмкость берётся
из descriptor NVFlash, независимого электрического измерения нет. Размер VBIOS
и одинаковые hashes не подменяют проверку ёмкости. Если EEPROM не подтверждён,
обычный проверенный VBIOS сохраняется отдельно, общий результат partial.

## Артефакты и отмена

`events.jsonl` всегда содержит app version, UTC, action_id, выбор/проверки,
фактические command/argv, PID, elapsed, exit codes, ошибки, file sizes/SHA.
Эти события также передаются основному logger. `tool.json` содержит точную
версию, SHA, источник, protected cache и WinVerifyTrust статусы. Raw stdout/
stderr всех вызовов хранятся полностью отдельными файлами независимо от AllLogs.
Manifest обновляется через временный файл и atomic rename/MoveFileEx.

Обычные файлы: pass1.rom/pass2.rom/vbios.rom. Полные:
eeprom-pass1.rom/eeprom-pass2.rom/eeprom-full.rom. Все raw stdout/stderr
сохраняются по отдельным меткам list/check/save/compare с размерами/SHA.

Ошибки/отмена сохраняют ROM partial и их непроверенные snapshots, raw и причину.
Отмена завершает текущий vendor read, затем запрещает следующий. Watchdog10мин
или32МиБ raw-вывода возвращает pending с PID; reader не убивается. Отдельный
waiter удерживает lease до завершения процесса, чтобы второй dump не захватил
его driver. Для зависшего vendor cleanup дальнейшее решение принимает человек.

## Runtime и статическая проверка

Windows original-byte candidate EXE SHA:
`c0620a7be0ffdad3782d9ddf238d1a19c31b587cbcfee7036819c8a50e2d89cd`,
версия5.867.0, EXE unsigned. Две embedded SYS reference1.21.0 имеют Valid
Microsoft WHCP подписи. Компиляция и protected RCDATA cache фиксируют SHA.
Перед запуском проверяется offline WinVerifyTrust SYS reference. Активные или
неизвестные NVFlash службы/SYS не захватываются; проверенные остановленные
остатки обрабатываются по правилам раздела выше.
Используется прежний общий runner AFU/FPT; старые /O,-I,-d команды сохранены.

Linux отдельный x86_64 ELF15909600байт, SHA:
`754693fb8f833be75601f95cb08a6e0cf538053cce84ab69020ec74ae96a6e70`.
В portable лежит `tools/NvFlash/nvflash`; исполняется только root-owned pinned
копия `/var/lib/MagicControl/readers/NvFlash-5.867/nvflash`, без symlink/hardlink
и group/world write. SHA сверяется перед каждым вызовом. У child очищено
окружение от LD_*; stdio направлены в raw файлы, stdin=/dev/null. Shell,
прошивальные скрипты, modprobe/rmmod команды MagicControl не запускает.

Источник обеих сборок — стандартные архивы ComputerBase; Windows x64 также
побайтно совпал с фиксированной независимой GitHub копией. Совпадение с OEM
пакетом не найдено, EXE не выдаётся за подписанный NVIDIA. Документы/embedded
third-party notices сохраняются в runtime/licenses. Публикация этого локального
пакета на GitHub не выполнялась и этим документом не разрешается.

[Подробная статическая evidence](../research/nvflash-reader-20261025/INTEGRATION.md).
Совместимость и побочные эффекты vendor reader на каждой RTX40/50 требуют
реального пользовательского запуска; данный билд проверен сборкой и файлами.

Офлайн-проверки0.1.196: по88parser/argv/catalogue проверок и9сценариев
реального collector с синтетическими OS/GPU/vendor stubs на каждой ОС.
[Матрица16моделей, результаты и границы проверки](../research/nvflash-dual-capture-20261025/REPORT.md).
