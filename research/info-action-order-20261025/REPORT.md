# INFO: порядок пяти кнопок,0.1.193

Поручение владельца: драйвер → сведения → дамп EC → дамп BIOS → дамп VBIOS.
Сделано в общей BiosActions/Geometry и обоих native адаптерах. Порядок
создания/добавления дочерних элементов соответствует Tab и визуальному ряду.
Существующие ID0/1/5/6 и обработчики действий сохранены; VBIOS — новый ID7.

Новый VBIOS пока disabled, без аппаратного обработчика обеих ОС. В подсказке
явно сказано, что NVFlash проверяется и collector не подключён. RU/EN/SC/TC
подписи/пояснение общие. Статическая проверка reader сохранена отдельно:
[NVFlash evidence](../nvflash-reader-20261025/REPORT.md).

## Границы правки и проверки

- Изучен Git эталон `d84343d` BIOS scrollbar.
- Переразбита только прежняя строка кнопок; подписи измеряются фактическим
  шрифтом и DPI через localizedDrawText/Pango. Сохраняются общий renderer,
  штатное сокращение текста в тесном окне и исходная высота30DIP.
- Не изменены pad12/gap6, heading/snapshot/update/secondary rows, формулы
  bottom/card/inventory, viewport width, lane, paint, wheel, drag и scroll.
- `git diff --check` для изменённых src/docs прошёл.
- Build-All завершил общий0.1.193 Windows/Linux GUI+mctl, обновил обе рабочие
  portable и проверил версии/SHA. Первое staging Windows остановилось на
  занятом EXE; после сообщения владельца о выходе через трей выполнен Resume
  того же номера. Приложение помощником не завершалось.
- Файловая проверка536 application inputs совпала с build receipt. Все четыре
  staged бинарника побайтно равны build результатам. PE subsystem GUI2/CLI3,
  CLI asInvoker и ELF version strings проверены чтением файлов.
- Все41 original runtime payload в каждом Windows EXE совпали с каталогом по
  размерам и SHA256; AFU/FPT16/FPT19 collector/validator побайтно совпали с0.1.191.
- Код управления ядра/протоколов не изменён. NVFlash не добавлен в runtime.

Хэши/размеры/проверки: [verification.json](verification.json).
Чекер читает файлы и PE/ELF/resources; не запускает продукт, утилиты или драйверы.
Тесты и GUI не запускались, визуального live-испытания не было. Обращений к
железу/EC, GitHub публикации/архивов и изменения пользовательских настроек нет.
