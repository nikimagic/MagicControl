# INFO: EC platform и полный регион ROM ID,25.10.2026

Поручение: сразу после «Версия SMBIOS» две строки — EC7C3 HEX/decimal и
все16байт770–77F, обновление по «Обновить сведения», при ошибке последний
успешный снимок с временем, неизвестные байты??, без подстановки00/FF/55AA.

## Реализация0.1.198

Общий core EcIdentityInventory хранит optional7C3 и16optional byte slots.
770–77D —14ROM-ID bytes;77E–77F — служебный хвост. Формат platform0x07(7),
region —16uppercase HEX tokens через пробел. Успешно прочитанный00 илиFF
остаётся значением, а не признаком отсутствия.55AA по умолчанию не добавляется.

Две строки вставляются одним core helper сразу после SMBIOS, обе GUI используют
прежние inventory rows, измерение текста/перенос и inventoryCardGeometry.
Старый cache при отображении получает?? placeholders без нового чтения EC.
Новые строки не меняют header, scrollbar/gutter/padding или порядок5кнопок.
Captions добавлены в RU/EN/SC/TC. Служебный metadata section скрыт общей
informationInventory projection, в raw cached JSON остаётся для сохранности.

## Чтение и доступность

Windows переиспользует тот же readonly transport, что существующий startup
ROM observer: reviewed installed driver для GM5/L16/HYDROC, реальные SMBIOS
profile/identity probes, исключение7C3 из precondition и отдельные model guards.
Открывается только createWindowsAcpiDiagnosticBackend, writable=false.
В одной transport transaction stable-чтение7C3 и770+i(i=0..15). Проверка
соседних чтений/ошибок не фильтрует сами byte values. Отказ одного byte
оставляет только его slot unknown; отмена прекращает следующие чтения.
Ни battery/FAN controller, restore, factory backend, запись EC или установка
драйвера не создаются. Старый startup_read всё ещё читает один770 до restore.
Успешное INFO чтение770 также передаётся прежнему observeRomId для rom_id.

Допуск чтения не расширен: WUJIE, GM6, GM5, L16, HYDROC через прежние
reviewed profiles. Honor/Unknown и остальные catalogue-only модели получают
недоступность/?? вместо чужой EC-карты. Linux extended EC backend отсутствует:
он не подменяется256байт ec_sys, отображает?? и реальную причину недоступности.
Открытие INFO обеих GUI загружает cache; новое collection только по кнопке
либо явному диагностическому/CLI запросу. Linux теперь также сохраняет cache.

## Снимок и журнал

Полный успешный снимок — все17полученных byte values вместе с local timestamp.
При неполном новом чтении InventoryCache возвращает предыдущий полный snapshot
только при совпадении scope компьютера; данные разных попыток не смешиваются.
UI показывает старые значения и «сохранено <time>; чтение <time> недоступно».
Без полного предыдущего snapshot видны текущие прочитанные bytes и?? slots,
с пометкой неполного чтения. Cache переносит именно bytes, не готовые
подставленные значения. Raw response/errors/source и время попытки/показанного
snapshot передаются logger в info_ec_identity даже при отображении старых bytes.

Scope — actual board/product и SMBIOS Type1 UUID, либо Windows install ID
fallback. Семейный UEFI firmware ID и UniWillVariable bookkeeping здесь не
используются. Scope защищает именно merge старого снимка в новую попытку;
сама INFO, как прежде, может показывать явно сохранённый inventory до refresh.
Структура Type1 UUID сверена с первичным
[EDK2 SmBios.h](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/IndustryStandard/SmBios.h).
UUID нужен только для local cache, никаких запросов к OTA/внешним серверам нет.

Metadata использует существующий version3 inventory JSON/атомарный cache.
Отдельного фонового poll, второго файла данных или parallel UI renderer нет.
Пользовательские INI/CSV и старые логи не изменяются этой задачей помощником.

## Проверка и ограничения

Приложение/EC/ACPI/driver/GPU/vendor читалки помощником не запускались.
В этой задаче новые тесты не добавлялись и не запускались: это соблюдает
AGENTS.md; предыдущие запрошенные BIOS/NVIDIA тесты закрыты bf92d5f/0.1.197.
Выполнена общая компиляция Windows/Linux GUI+CLI0.1.198 и file-only сверка
обеих portable, версий/SHA и47embedded resources каждой Windows программы.
Подробности в verification.json. Реальное чтение этих полей и поведение
при аппаратном отказе требуют пользовательского запуска «Обновить сведения».

AFU/FPT/NVFlash reader bodies/runtime и BIOS/NVIDIA validators не менялись.
Geometry.h/DeviceHeader и native scrollbar механика сохранены. Первая попытка
сборки обнаружила source_changed во время завершения cache projection;
общая сборка продолжена тем же номером198 с пересборкой обеих ОС.
