# HYDROC-16 G2: профиль и диагностика

SMBIOS: `Eluktronics Inc.` / `HYDROC-16` / `HYDROC-16 G2`.
Исходный отчёт: BIOS `N.1.13ELU11`, EC SMBIOS1.12, Ultra9 275HX / RTX5090.
Официальный пакет пользователя: ControlCenter5.23.51.56 Eluktronics V4.
Разбор и хэши: `research/devices/hydroc-16-g2/oem-5.23.51.56/`.

## Чтение EC

`DiagnosticsWindows` создаётся только по запросу дампа, отдельно от legacy
`MainWindow::ec_`. Использует общий транспорт `uniwill-acpi`, его recursive
межпроцессный gate и общий `captureEcBlocks`. Аппаратные capabilities профиля
кроме batteryCharge выключены; узкий battery transport не выбирается
обычной фабрикой EC. Диагностический backend остаётся read-only.

Перед открытием проверяются точный SMBIOS, единственный PnP `ACPI\INOU0000`,
service `UWACPIDriver`, путь SYS внутри Windows и плоский SHA256 извлечённого
родного драйвера11.2.18.347:
`e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2`.
При другой версии/хэше сохраняются сведения об установленной копии и причина
отказа. Установка драйвера, замена SYS и остановка службы OEM не выполняются.
Для EC нужен установленный родной драйвер; PawnIO выполняет отдельную роль
CPU/MSR/SMBus и не заменяет его.

Используется только IOCTL `0x9C40A488`: DWORD адрес,16байт выходного буфера.
DefaultTool передаёт4/4, но native SYS копирует переменную ширину ACPI integer,
поэтому сохранён защитный16-байтовый output. Вход4байта также исключает
пустой цикл драйвера по4МиБ, применяемый OEM native wrapper.
Окно чтения строго `0x000–0xFFF`; write отвергается внутри backend до IOCTL.
Handle открыт RW из-за access mask самого IOCTL; это не право приложения
использовать write IOCTL. `info.writable=false`, counters writes=0.

Две стабильные начальные выборки ProjectID/BIOS-ID/capability должны отличаться
от сплошного0/FF: OEM SYS может скрыть внутреннюю ошибку ACPI как успешный
WDF запрос. Отдельный нулевой байт не отбрасывается. Снимок не атомарный.

## Пакет дампа

Общие файлы `ec.bin`, `ec-valid.bin`, `ec.csv`, `sensors.jsonl`, `report.txt`.
`dependencies.json` сохраняет отдельные снимки PawnIO до и после сбора:
SCM installed/running/state/exit codes, тип и запуск службы, ImagePath SYS,
наличие и размеры файлов runtime по общему списку упаковки. Проверка сама
драйвер не запускает. Ошибка SCM остаётся unknown/null, а не «не установлен».
Наличие файлов не доказывает загрузку модулей: результаты чтения и HRESULT
каждого поставщика остаются в `sensors.jsonl` и `session.jsonl`.
`ec-interface.txt` содержит PnP, версию, INF, ImagePath, фактический/ожидаемый SHA,
BIOS и начальные пробы. Он сохраняется также при отказе guard/открытия.
`ec-protocol-candidates.json` содержит сырые ProjectID/BIOS-ID, RamFan1.5 bit,
LC-table bit, keyboard subtype, кандидаты батареи и банк F00–FFF.
Ни один формат FANs не объявляется выбранным по одному capability bit.

Default/Max и NVML fan setters для этого профиля выключены в общем API дампа,
в том числе при вызове из CLI или NoName. Реализация не пишет FAN tables,
батарею, подсветку, MUX и power limits. Явный сохранённый профиль батареи
восстанавливается отдельно после запуска/сна; операция дампа не запускает restore.

## Остальная телеметрия

Intel DTS/RAPL читает только проверенные MSR на CPUID family6 C5/C6,
с runtime valid bits/units и прежними ограничениями значений. CPUID signature,
family/model/stepping включены в диагностический fast timeline.
Для HYDROC разрешён прежний read-only SPD5118 путь через подписанный I801
модуль с собственным guard контроллера и общим SMBus mutex. Доступность шин и
датчиков определяется реальным ответом, а не названием модуля памяти.
PCH, NVML, NVAPI и IGCL используют существующие общие источники.

Linux распознаёт модель, показывает общий UI, но не получает Windows EC
transport. Windows packet/driver не имитируются через256байт ec_sys.
Включение профильных записей требует отдельного разбора фактического EC-дампа.

## Штатная защита батареи,19.10.2026

Восстановленный точный сервис BatteryProtection2:7A6.4–5=00/10/20 —
Производительность/Баланс/Здоровье. Проценты этим профилям не назначаются.
Общий NativeBatteryController используется также GM6, с отдельными identity
и файлами настроек. Для HYDROC проверяет SMBIOS, ProjectID26,78E.6, PnP/service
и pinned SYS hash перед записью. Ни одна запись FANs/PL/подсветки не разрешена.
Fresh masked RMW/readback, ограниченный rollback при ошибке, отказ при OEM
GCUService/AiStoneService. Успех сохраняет BatteryProtection в
hydroc16g2-device-settings.ini; только этот явный ключ восстанавливается
после запуска/сна. На выходе сброса нет. Linux: те же подписи, writer недоступен.

Fan/light source review и найденный factory-table mailbox:
research/devices/hydroc-16-g2/oem-5.23.51.56/CONTROL-REVIEW-20261019.txt.
Подключение в коде и readback не доказывают физическую остановку зарядки.

Поставка драйвера19.10.2026: полный неизменённый SYS/INF/CAT11.2.18.347
с provenance находится в runtime/Uniwill/11.2.18.347, включён в ресурсы GUI
и Windows portable drivers/Uniwill/11.2.18.347. Установка — только явное
действие. Runtime проверка installed SYS по pinned SHA не ослабляется.

## Подсветка: descriptor candidates,20.10.2026

Точный OEM `LM_Manager` сначала выбирает ITE HID, затем EC SingleZone fallback.
В старом дампе G2 `0x766.2=0`, поэтому перенос EC RGB writer Wujie/GM6 не разрешён.
Pure helper `src/protocols/uniwill/hydroc16g2/KeyboardHid.h` содержит только
exact OEM VID/PID/Usage/Page и минимальную длину Feature Report. На HYDROC и
Unknown это исключительно аннотация descriptor evidence, `permission=false`.
Отдельные lightbar PID не считаются клавиатурой; неподходящие Usage/feature
length/UsagePage отвергаются. `HIDD_ATTRIBUTES.VersionNumber` не заменяет FW80.

Следующий дамп с общей секцией native HID metadata покажет provisional family
и необходимость FW80/EC73C/ProjectID/calibration. Сам helper никаких HID reports
не отправляет. Нужен точный фактический instance с VID/PID/caps/FW, прежде чем
разрешать writer или включать controls. Capabilities, карточки и скролл не меняются.
Welcome timeout1A не равен idle timeout: штатный idle — host timer в секундах
с keyboard/mouse activity. Подробности и hashes exact источников:
`research/analysis-20261020/keyboard-backlight-agents/hydroc.txt` и `.json`.

## Проверочные проценты батареи20.10.2026

По явному поручению владельца подключены существующие100/95/85 кнопки через
общий NativeBatteryController trial, с отдельным нижним80. Exact восстановленный
BatteryProtection2:585–635 (SHA25601088af4344d6c9739fd740ba935b943733f096960e5a8b82ad7b3866165aa26):
upper7B9.low7, restart7D0.low7, preservebit7. Не подстановка native0/1/2.
Отдельный backend разрешает только эти два поля, с fresh masked RMW под
общим recursive transaction gate; native7A6 backend не расширяется.
SMBIOS/Project26/RAM1.5/PnP/service/installed SYS pin сохраняются перед записью.
При ошибке — парный owned rollback; чужие/изменившиеся поля не перезаписываются.
Нет WMI/UEFI/FANs/PL/платформы или автосохранения экспериментального процента.
Отсечку тока и restart проверяет владелец; readback не выдаётся за исполнение.

Read-only fallback68циклов из word4A6 подтверждён собственным BatteryInfo:77–83.
Одна достоверно перечисленная физическая батарея, стабильные слова/ProjectID,
никаких управляющих workers. Health и зарядкаВт остаются Windows Battery Class;
переноса GM6 capacity/ISDB/charger map нет. Карточки/скроллбар не меняются.
Инструкция: research/devices/hydroc-16-g2/battery-owner-20261020/OWNER-CHECK.txt.

Порт зарядки уточнён по собственному MyEcCtrl:171–197: DC=7CC.bit0,
Type-C=7CC&36, rating=max(7CD,7CE). В дампе7CC=81 означает DC и отсутствие
Type-C flags. Общая строка SETs выводит DC либо USB-C (без номера), только
при свежем подтверждении AC Windows. Пустые биты не заменяют WindowsAC=false.
Type-C rating отделён от текущей зарядкиВт; нулевые/FF не показываются как0Вт.
Ваттность круглого адаптера и приоритет одновременно подключённых входов
пока не подтверждены. Getter ничего не записывает.
