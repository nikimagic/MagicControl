# AMI/FPT/NVFlash: причина отказов /kp и официальное происхождение

27.10.2026. Только скачивание,чтение файлов,проверка сертификатов и журналов.
Утилиты поставщиков не запускались,драйверы не загружались,настройки защиты
Windows не изменялись. Product runtime и его pinned bytes оставлены прежними.

## Основной результат

Предыдущий вывод о возможной непригодности всех драйверов по одному `/kp`
был слишком широким. У **всех шести проверенных SYS** есть дополнительная
подпись **Microsoft Windows Hardware Compatibility Publisher**.

SDK signtool `/kp /v` выводит `Signature Index:0 (Primary Signature)` и
останавливается на первой цепочке. Он не принимает `/all`, `/ds` или `/ms`
вместе с `/kp`,поэтому обычным `/kp` нельзя объявить проверенными обе подписи.
Первая подпись может быть отвергнута driver-policy,а дополнительная Microsoft
при этом полностью действительна.

Проверены отдельно primary/index0,secondary/index1 и автоматический выбор
через **WinVerifyTrust + WINTRUST_SIGNATURE_SETTINGS**. Для проверки Microsoft
driver chain использован **DRIVER_ACTION_VERIFY** и DRIVER_VER_INFO из
официального Windows SDK. Никаких доверенных корней или исключений не добавляли.

| Встроенный драйвер | Primary:прежний `/kp` | Microsoft secondary:DRIVER_ACTION_VERIFY | Автовыбор того же driver provider |
| --- | --- | --- | --- |
| AMI amigendrv64.sys | Недоверенный для этой политики корень вендорской цепочки | 0x00000000 | 0x00000000 |
| FPT16 driver-reference | Requested usage первой Intel цепочки | 0x00000000 | 0x00000000 |
| FPT19 driver-reference | Недоверенный корень первой Intel цепочки | 0x00000000 | 0x00000000 |
| NVFlash driver1 | Непостроенная первая NVIDIA DVS Dev цепочка | 0x00000000 | 0x00000000 |
| NVFlash driver2 | Непостроенная первая NVIDIA DVS Dev цепочка | 0x00000000 | 0x00000000 |
| Insyde H2OFFT64.sys | Первая Insyde цепочка;ранее отдельно проверен CAT | 0x00000000 | 0x00000000 |

В самой дампилке сейчас используется автоматический WinVerifyTrust Authenticode.
Его поведение также воспроизведено над теми же файлами: **успех у всех шести**,
включая оба NVFlash. Ложного отказа этого preflight в текущем коде не найдено.
Менять его на небезопасное «разрешить любой driver» не требуется.

`DRIVER_ACTION_VERIFY` подтверждает доверенную Microsoft driver signature.
Это не загрузка драйвера и не исчерпывающая проверка HVCI,всех WDAC/blocklist
правил,совместимости устройства или разрешений BIOS на чтение SPI.

## Официальные источники подтверждены байтами

### AMI

Повторно скачан опубликованный AMI архив:
https://f.hubspotusercontent10.net/hubfs/9443417/Support/BIOS_Firmware_Update/Aptio_V_AMI_Firmware_Update_Utility.zip

- HTTP200,20 713 273байта,Last-Modified06.03.2026.
- SHA256 `b59f141c37d5f55179459f6e07f0d1ed81fa742d1998b50954f1f9d6f3955307`.
- AFUWINx64 5.17.00.0153 иamigendrv64.sys **совпали с нашими побайтно**.
- EXE не подписан;SYS имеет подпись AMI и Microsoft secondary. Отсутствие
  подписи user-mode EXE не является отсутствием подписи kernel driver.

Другая подписанная версия для устранения конкретного `/kp primary` не нужна:
Microsoft-подпись уже находится в неизменённом официальном SYS.

### Intel FPT

Повторно сверены сохранённые оригинальные OEM-пакеты и встроенные drivers:

- **16.1.25.1871:** официальный GIGABYTE BIOS_NHxxPNP_RCxxKE_FB05.zip,
  https://download.gigabyte.com/FileList/BIOS/BIOS_NHxxPNP_RCxxKE_FB05.zip
  SHA256 `5d9e2edb0b405708eef6520c3327d3cb75d5dac0d33e65e1e14a41fc8c2b49b5`.
- **19.0.5.1948:** официальный COLORFUL iGameCenter2.0.0.82,
  https://download.colorful.cn/iGameCenter/iGameCenter-2.0.0.82-2026.09.12-Installer_Prod.exe
  SHA256 `ce7f0b697dbc64662541838aed2d914508bb085db4b8e6186749b0dd4a4f8c53`.

Оба EXE совпали с нашими;оба driver-reference найдены внутри оригинальных
EXE как точные неизменённые диапазоны байтов. У обоих есть действительная
Microsoft secondary. FPT19 не подменяет FPT16 на старом Intel чипсете:
проверка подписи не отменяет существующий CPU/ME/platform admission.
В COLORFUL также найден ранее извлечённый FPT16.1.30.2382,но необходимости
менять на него рабочий протокол ради подписи не установлено;его не применяли.

### NVIDIA

Официальная страница NVIDIA:
https://nvidia.custhelp.com/app/answers/detail/a_id/5665/~/nvidia-gpu-uefi-firmware-update-tool-for-rtx-5060-series

Скачан напрямую с NVIDIA **Firmware Updater2.0**:
https://us.download.nvidia.com/Windows/uefi/firmware/2.0/NVIDIA_UEFI_Firmware_Updater_2.0-x64.exe

- HTTP200,24 798 336байт,EXE подписан NVIDIA Corporation,Authenticode Valid.
- SHA256 `fe7cedcfe0eb45336b7ef8a3f4acda286909ef83370f5fbb473a62dc3edf9123`.
- Без запуска извлечены два Native PE-драйвера из смещений0x128DEA0/0x1299170.
- Их SHA256:
  `79050454d06ab7b6607bca09386f704df021c3c367cc0743de6845301bb192ac`;
  `8c2d9847b70c3dc1aac5d9e5330c67deb86f369dcd3ca85845adf2a001088dde`.
- **Оба в точности совпали с driver1/driver2 нашего NVFlash5.867.0.**

Таким образом,официальное происхождение самих драйверов теперь подтверждено
прямым подписанным пакетом NVIDIA,а не только зеркалом NVFlash.
Вся NVIDIA Updater2.0 не заменяет нашу NVFlash-дампилку:это отдельный updater
с собственными firmware payloads/ограничениями. Его не запускали и runtime
не подменяли. Исходный nvflash64.exe по-прежнему unsigned,его bytes pinned;
это не меняет действительность Microsoft-подписей двух kernel-драйверов.

## Защита Windows и дополнительные ограничения

- Win32_DeviceGuard сообщает VBS running и SecurityServicesRunning содержит2:
  **HVCI/Memory Integrity работает**. РеестрHVCI Enabled=1.
- VulnerableDriverBlocklistEnable=1. Не выключался.
- Confirm-SecureBootUEFI отказал в требуемой привилегии. Состояние Secure Boot
  этим исследованием **не установлено**,не трактовать ошибку как выключенный SB.
- Среди последних3000 записей CodeIntegrity за ограниченный10-дневный запрос
  записей с именами наших reader-драйверов не найдено. Это не доказательство,
  что ни на одном другом ПК никогда не было блокировки.
- Скачан официальный Microsoft blocklist10.0.29545.0 через
  https://aka.ms/VulnerableDriverBlockList,но **не установлен**.
  У шести наших файлов не найдено точных Deny совпадений по полным SHA1/SHA256
  и Authenticode SHA1/SHA256. Page-hash и signer/version deny rules этим простым
  сравнением не оценивались;BLOCKLIST.json явно фиксирует границы проверки.

Подпись — только одна проверка. Если реальная загрузка всё же будет заблокирована,
нужны её Win32/NTSTATUS и соответствующее событие CodeIntegrity3077/3033.
Отказ HVCI,blocklist,неподходящий чипсет и SPI read protection нельзя выдавать
за одну ошибку `/kp`. Никаких обходов политики,TESTSIGNING,подмены сертификатов,
legacy vulnerable loaders или отключения Secure Boot/HVCI не добавляли.

## Что исправлено

Исправлена интерпретация прежнего signature audit:сырой `/kp primary` больше
не используется как вывод о непригодности целого dual-signed SYS. Сохранён
воспроизводимый пассивный inspection tool и полные официальные provenance checks.
Старые рабочие vendor bytes,протоколы и продуктовые binaries не менялись.
Пересборка продукта ради замены уже совпадающих официальных драйверов не нужна.

## Воспроизводимые файлы

- inspect_trust.py/TRUST.json/trust-output.txt:36 отдельных WinVerifyTrust вызовов.
- inspect_official.py/OFFICIAL.json:свежий AMI и два Native PE из NVIDIA2.0.
- inspect_origins.py/INTEL-ORIGINS.json:архивы OEM и точные embedded SYS для FPT.
- DOWNLOADS.json/BLOCKLIST-DOWNLOAD.json:официальные URL,даты и хэши загрузок.
- WINDOWS-POLICY.json:read-only снимок статуса защиты и ограниченного CI-запроса.
- inspect_blocklist.py/BLOCKLIST.json:границы и результат статического сравнения.

Microsoft primary references:
- https://learn.microsoft.com/en-us/windows/win32/api/wintrust/ns-wintrust-wintrust_signature_settings
- https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust
- https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Security/CodeSigning/cpp/codesigning.cpp
- https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/design/microsoft-recommended-driver-block-rules
