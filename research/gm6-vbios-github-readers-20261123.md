# Сравнение программных считывателей NVIDIA ROM

Сохранены текстовые копии в `Reference_Projects/NVIDIA_ROM_Readers_20261123`.
Проверка статическая: исходники и документация, без запуска на GPU.

| Копия | Источник | Результат разбора |
| --- | --- | --- |
| envytools-nvagetbios.c | https://github.com/envytools/envytools/blob/master/nva/nvagetbios.c | Выбор по номеру карты. По умолчанию PRAMIN, затем PROM. PROM до 1 МиБ; PRAMIN читает 64/128 КиБ из VRAM. Временно меняет регистры GPU, проверяет 55 AA и PCIR, пишет результат в stdout. Не определяет ёмкость чипа. |
| nouveau-shadow.c | https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/nouveau/nvkm/subdev/bios/shadow.c | Выбирает источник VBIOS по результату проверки цепочки образов. Извлекает данные по длинам образов, а не размеру микросхемы. |
| nouveau-shadowrom.c | https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/nouveau/nvkm/subdev/bios/shadowrom.c | Использует устройство драйвера, отключает PCI ROM shadow на время доступа, определяет смещение PCI по IFR и читает из окна PROM не дальше 1 МиБ. |
| nova-vbios.rst | https://github.com/torvalds/linux/blob/master/Documentation/gpu/nova/core/vbios.rst | Описание IFR v1/v2/v3 и источников образа, пригодное для сверки нашего парсера. Не протокол чтения физической SPI. |
| cmp170hx-vbios.md | https://github.com/Consensus-Protocol/cmp170hx/blob/main/docs/hardware/vbios.md#dumping-the-vbios | В примере GA100 явный PCI BDF, read-only mmap PCI resource0, фиксированный диапазон 1 044 480 байт, проверка NVGI, сохранение в файл. Разделяет результат окна GPU и независимое чтение SPI. |
| vfio-windows-aio-README.md | https://github.com/terminal-index/vfio-windows-aio | Внешняя обёртка для обычного NVFlash --save. Отдельного считывателя EEPROM и проверки ёмкости нет. |
| flashrom-gfxnvidia.c | https://github.com/deepin-community/flashrom/blob/master/gfxnvidia.c | Подбор по списку старых PCI ID, backend BUS_PARALLEL. RTX 4060 Laptop 10DE:28A0 в списке нет. |
| flashrom-nv_sma_spi.c | https://github.com/flashrom/flashrom/blob/main/programmers/nv_sma_spi.c | Ищет управляющий USB–SPI интерфейс NVIDIA SMA (0955:CF11), а не GPU 10DE:28A0 по PCI. |

На GM6 обычный сохранённый контейнер уже 2 048 000 байт, а начало
второй PCI-цепочки находится по 0xE9400. Ограничение PROM в 1 МиБ в
приведённых исходниках относится к окну реализации, не к размеру
чипа GM6. Это не эквивалент существующего NVFlash-файла.

Из этих проектов применимы раздельные метки источника (образ,
окно GPU, физический чип), точная PCI-привязка и несокращённое
сохранение исходного файла. Оснований переносить адреса или размеры
GA100/старых NVIDIA в MagicControl нет. Для полного чтения без
программатора на GM6 ближайший кандидат остаётся штатный NVFlash
entire-save с отдельной проверкой полноты. Отсутствие дескриптора
в check следует отмечать отдельно от результата самого чтения.
