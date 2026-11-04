# MagicControl

Управление и диагностика ноутбуков и mini-PC: FANs, режимы мощности, батарея,
подсветка, KEYs, GPU, сведения и дампы. C++20, Win32/GDI и GTK4; GUI и консоль
`mctl` используют общие ресурсные операции. Доступность задают фактический
backend, устройство, драйвер и права; совпадение CPU/имени модели не разрешает EC.

## Проверенный срез

На **01.11.2026** последний сверенный общий билд — **0.1.428**, с ранней CLI read-only защитой и межпроцессной защитой логов/сведений (код — коммит этой версии).
Windows/Linux GUI+CLI и обе рабочие portable имеют эту версию; build manifest
сообщает `complete`, обе платформы `verified`. Это датированный срез, не обещание
автоматически актуального номера. Источники текущей версии и hashes:
`build/product-build.json`, `build/product-version.txt`, `MagicControl/*/build-version.txt`.
Версия локального архива и опубликованного релиза может отличаться от working build;
локальные пакеты перечислены в [GitHub_Releases/README.txt](GitHub_Releases/README.txt).

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

Каталог содержит17 известных профилей (16 ноутбуков и HPBSD) и отдельный Unknown.
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
У батареи различаются штатные Full Capacity/Balanced/Healthy, процентные запросы,
aging/history и реально применённое состояние; действующий контракт —
[BATTERY](src/platform/windows/BATTERY.md). Firmware readback не доказывает
физическое удержание заряда, измеренное потребление или полноту SPI.

Sc использует одну core очередь поворота на180° с native readback. Windows — CCD;
Linux — KDE/Plasma Wayland/KScreen либо X11/XRandR. GlobalShortcuts portal может
попросить разрешение; нужен корректный kernel keymap. GNOME/wlroots Wayland
writer не подключён, KEY_UNKNOWN не подменяется Sc. См. [LINUX-SC](docs/LINUX-SC.txt).

## Разработка

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

Лицензия исходников не определена. Условия сторонних компонентов — в notices и
source offers соответствующих пакетов. Документационная правка не меняет лицензию.
