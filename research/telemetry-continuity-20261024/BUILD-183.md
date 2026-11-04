# MagicControl0.1.183: подтверждённая сборка

Tools/Build-All.ps1 и повтор-Resume завершили обе ОС. Рабочие Windows/Linux
GUI/CLI0.1.183 совпадают с build по SHA256; все application inputs сверены.
Первый проход обнаружил изменение исходников и не считался завершённым.

Общее ядро DisplayContinuity, сохранение EC-health с исходной временной меткой,
UI cache для FANs/SETs/KEYs/GPU, SingleZone Off/stop effects/host cycle,
активный Windows display LUID и отдельный Linux boot_vga, NVML conflict0,
явные UI selected/GPU selection/retained fields в логах. Raw setters/контрольное
чтение/защита/diagnostic collectors не используют presentation cache.

Тесты, GUI/helper и аппаратные операции не запускались. Физический свет,
драйверное P0/потребление, все16 ноутбуков физически не проверены.

После завершения183 подготовлено усиление identity-проверки батареи и timestamp
холодного первого percent-GPU observation. Эти два изменения идут в следующую
сборку184. Коммит183 использует точные предыдущие снимки Monitoring.h и
DisplayContinuity.h; SHA256 сверены с application_inputs завершённого183.
Рабочие новые изменения сохранены и не потеряны.
