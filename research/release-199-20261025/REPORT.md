# Локальные пакеты0.1.199,25.10.2026

Прямое поручение: подготовить релиз в папку. Уточнение владельца: mctl нужен
только ему в рабочих portable; в релизные архивы не включать. Публикации/push нет.

В GitHub_Releases подготовлены Windows ZIP и Linux TAR.GZ, распакованные папки,
SHA256SUMS-0.1.199.txt, RELEASE-NOTES и VERIFICATION-0.1.199.json. GUI source
revision e01208f, application inputs чистые и совпадают с завершённой общей
сборкой199. Упаковочные Tools/docs/templates не меняют captured application
inputs и не требуют нового номера продукта. Рабочие GUI/mctl обеих ОС остались
той же0.1.199 с исходными SHA; никакие пользовательские INI/логи не брались.

Общий Package-Releases.py теперь использует canonical Linux stager: он включает
в том числе pinned tools/NvFlash/nvflash и notices. Раньше отдельный список
Linux GUI-only зависимостей пропускал новый VBIOS reader. Vendor ELF не strip-ится,
его SHA754693fb... остаётся прежним, в TAR сохранён executable mode. Strip и
readelf выполнялись только над копией нашего GUI. Windows resources47совпали
побайтно с закреплёнными оригиналами.

Первая текущая неопубликованная заготовка содержала CLI; после уточнения владельца
она убрана из release folder в build/release199-superseded-cli. Старые опубликованные
версии не заменялись/удалялись. Окончательные архивы пересозданы без mctl;
запрет mctl/mctl.exe в release manifest/file tree закреплён в упаковщике.
platform_id/rom_id/bios-inventory.json и другие личные файлы также запрещены.

File-only проверка verify_packages.py сверила manifest и каждый архивный байт,
version/platform/product/source revision, SHA, чистые настройки, private-data
exclusion, TAR types/exec modes, ELF, NVFlash pin/notices и неприкосновенность
личных CLI. Все старые entries глобального SHA256SUMS сохранены. Глобальные
README/SHA имели изменения до этой задачи: stage добавляет только новые199
записи, оставляя предшествующие пользовательские правки вне коммита.

AFUWINx64 и оба FPTW64 — Windows x64 binaries, штатные Linux BIOS APIs возвращают
available=false. Для системного BIOS под Linux reader не подключён. NVFlash
имеет отдельный native Linux ELF/backend и включён. Это программное подключение,
не проверка работы на физическом GPU/ноутбуке. Приложения/driver/vendor tools
и тесты приложения при упаковке не запускались; WSL использовался для
copy/strip/readelf файлов. Точные размеры и SHA находятся в VERIFICATION.
