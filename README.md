# MagicControl

Управление и диагностика ноутбуков и mini-PC: FANs, режимы мощности, батарея,
подсветка, KEYs, GPU, сведения и дампы. C++20, Win32/GDI и GTK4; GUI и консоль
`mctl` используют общие ресурсные операции. Доступность задают фактический
backend, устройство, драйвер и права; совпадение CPU/имени модели не разрешает EC.

## Проверенный срез

Сверено **17.11.2026**: рабочие Windows/Linux GUI+CLI и обе portable —
**0.1.492**. `build/product-build.json` имеет статус `complete`, обе платформы
`verified`; четыре исполняемых файла сверены с SHA-256 манифеста, различий
в его входных файлах не обнаружено. Основание сборки — `23d0f84` (коммит
0.1.490) и отмеченное манифестом рабочее дерево. Номер коммита и номер
portable не подменяют друг друга. Сборка датирована 07.11.2026; новые
аппаратные проверки при этой сверке не выполнялись.

Версии и хэши следующих сборок смотреть в `build/product-build.json`,
`build/product-version.txt` и `MagicControl/*/build-version.txt`.
Локальный или опубликованный архив может иметь другую версию.

В GUI нет собственных thread/jthread/async creators; команды INFO/KEYs/SETs/FANs/
GPU/Update/Sc используют общий runtime. Нативный Windows OSD сохраняет свой HWND
message loop через core actor; Linux — GTK/D-Bus. Это описание кода и сборки,
не заявление о физической проверке каждой функции на всех устройствах.

## Начало работы

Рабочие portable: `MagicControl/windows/MagicControl.exe` и
`MagicControl/linux/MagicControl`. Личная SSH-консоль — `mctl.exe` / `mctl` рядом
с GUI. CLI help/version не обращаются к железу. Команды, режимы владения и
ограничения платформ описаны в [CLI.md](docs/CLI.md).

- Windows: запускайте целиком подготовленную portable. Официальные OEM-драйверы,
  reader references и PawnIO payloads встроены в EXE; отдельные аппаратные функции
  требуют своих прав/подтверждённых drivers. Установка — явной кнопкой INFO.
- Linux: `./MagicControl`; релизный пакет также содержит `start.sh`. Нужны системные
  GTK4/GLib/GIO/Pango/Fontconfig/PulseAudio и подходящие ABI Ubuntu/Kubuntu26.04.
  Это не AppImage. Точные runtime requirements — в README пакета и linux-abi.txt.
- Обновляйте бинарники при штатно закрытом приложении. Личные INI/CSV, журналы,
  дампы и append-only identity/history сохраняются; не копируйте чужие настройки.
- Проверка обновлений открывается нажатием версии. Поддерживаются проверяемые
  локальные пакеты и GitHub; helper ждёт обычного выхода приложения. Порядок
  упаковки/обновления — [APP-UPDATES](docs/APP-UPDATES.md).

## Интерфейс и поддержка

| Вкладка | Назначение |
|---|---|
| FANs | Фактические каналы FAN/PWM, температуры, Default/Manual/online/Max и редактор при наличии backend |
| SETs | Native CPU/Custom, независимые настройки подсветки, батарея; только допущенные фактическим backend controls |
| KEYs | Системные и native переключатели, микрофон, OSD/Copilot; один keys-settings.ini |
| INFO | Сведения реальной машины, явная установка драйвера, сбор EC/BIOS/VBIOS/EEPROM evidence |
| GPU | NVIDIA/Radeon providers, точная identity, поддерживаемые драйвером лимиты и readback |

Каталог содержит **18 профилей** (17 ноутбуков и мини-ПК HPBSD),
включая Lenovo Legion KWCN, и отдельный fallback Unknown.
Точный состав — [DeviceRegistry](src/devices/DeviceRegistry.cpp), допуски —
[матрица поддержки](docs/DEVICE-SUPPORT-MATRIX.md). Профиль в меню не обещает
полного управления: INFO/системные KEYs и независимые GPU tools могут работать
при отсутствии model-specific EC writer. Windows raw карты не переносятся в Linux.

Экспериментальные USB-C Battery/Performance, raw platform/ROM buttons и Logger
в SETs скрыты общей visibility policy. Это не переключение identity или сброс
сохранённых настроек. USB-C priority writer отвергается как неподдержанный;
сырой статус не выдаётся за выбранный Performance-режим.

В KEYs OSD/Copilot хранятся в `keys-settings.ini`; Copilot — native firmware
resource, без F23/Win+C hook. Bluetooth не воспроизводится из INI при startup/resume.
У батареи различаются штатные режимы конкретного backend (в том числе
Full Capacity/Balanced/Healthy у поддержанных Uniwill), процентные запросы,
aging/history и реально применённое состояние; действующий контракт —
[BATTERY](src/platform/windows/BATTERY.md). Firmware readback не доказывает
физическое удержание заряда, измеренное потребление или полноту SPI.

Sc использует одну core очередь поворота на180° с native readback. Windows — CCD;
Linux — KDE/Plasma Wayland/KScreen либо X11/XRandR. GlobalShortcuts portal может
попросить разрешение; нужен корректный kernel keymap. GNOME/wlroots Wayland
writer не подключён, KEY_UNKNOWN не подменяется Sc. См. [LINUX-SC](docs/LINUX-SC.txt).

Custom общий для устройств: один редактор, слоты1/2/3 с мощностью и собственными
FAN-кривыми, общая библиотека и ресурсный вход. База Custom и дополнительные
параметры мощности задаются данными/возможностями backend. Модель хранит16
ячеек на канал; активное число точек — `DeviceProfile.fanPointCounts`.
Read-only штатная таблица не даёт разрешения записывать её.

Для BIOS/VBIOS общий `TelemetryGate` согласует паузу опроса, завершение уже
начатых наблюдений и владение reader. Активный защитный контур охлаждения
не отключается ради захвата. Gate принадлежит процессу; внешние программы
им не управляются. Ограничения и неподтверждённые случаи — в
[DIAGNOSTIC-DUMPS](docs/DIAGNOSTIC-DUMPS.md).

Текущие частоты памяти выбираются по CPUID/типу DDR и проверенным register
layouts. Номинальная скорость SMBIOS/SPD не выдаётся за текущее измерение;
неподтверждённые или недоступные значения остаются unknown. Состояние OEM
Overclock/FwVars Wujie и границы исследования описаны в
[WUJIE-OVCLOCK](docs/WUJIE-OVCLOCK.md).

## Разработка

В Git хранятся исходники, конфигурация и текстовая документация. Бинарные
зависимости, шрифты/изображения, portable, релизные архивы, логи и дампы
остаются локально. Свежему клону нужны отдельно предоставленные артефакты
по [EXTERNAL-ARTIFACTS](docs/EXTERNAL-ARTIFACTS.md), с проверкой provenance/SHA-256.

Исследования индексируются в [research/INDEX.md](research/INDEX.md).
Для материалов SensorProbe и EC см. [обзор архива](research/test_util/analysis/README.md),
[отчёт по температурам](research/test_util/analysis/TEMPERATURES-FULL-REPORT.md)
и [инвентаризацию датчиков](research/test_util/analysis/sensor_inventory.py).
`build` содержит используемые сборки/clangd/staging, одноразовые прогоны —
`out`; общие декомпиляции и внешние оригиналы находятся в соседних каталогах
[материалов](../КАТАЛОГ_МАТЕРИАЛОВ.md). Переносы продолжаются: старый путь
или отсутствие cache-файла не являются основанием для удаления данных.

Перед правкой прочитайте [AGENTS.md](AGENTS.md) и обязательный раздел
[WORKING-RULES](docs/WORKING-RULES.md). [PROJECT-MAP](docs/PROJECT-MAP.md) показывает
entry → owner → backend → readback → persistence; [WORKLIST](docs/ARCHITECTURE-WORKLIST.md)
содержит открытую работу. Исследования и старые версии сохраняются отдельно от
активных контрактов. Обычный поиск ограничен `.ignore`; evidence открывайте явно:

```powershell
rg --files src docs Tools cmake packaging
rg -n "executeGpuOperation" src
rg --no-ignore -n "нужный признак" research/конкретный-case
```

Общий Release Windows/Linux GUI+mctl:

```powershell
.\Tools\Build-All.ps1
# Незавершённый билд: .\Tools\Build-All.ps1 -Resume
```

Сборка создаёт одну версию, обновляет обе portable и сверяет4SHA. После успешного
общего билда обязателен отдельный подробный локальный commit. Тесты и hardware
не запускаются без явного поручения. Пакеты только по запросу через
`Tools/Package-Releases.py`; публикация/push/tag/signing — отдельное поручение.
`mctl` входит только в личные working portable, не в публичные архивы.

Условия официальной двоичной поставки — [packaging/LICENSE.txt](packaging/LICENSE.txt).
Условия сторонних компонентов приведены отдельно в notices/source offers.
Доступ к исходникам сам по себе не заменяет условий правообладателя; этот
документ не вводит новую лицензию.
