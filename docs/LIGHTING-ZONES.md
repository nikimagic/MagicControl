# Одна карточка подсветки и зоны

## Дополнение дампа: firmware80 и OEM flags,24.10.2026

Новый explicit dump на реальном HYDROC сохраняет `lighting-hid.json` и ту же
секцию в `inventory.json`. Это отдельный bounded helper-запрос; обычное
обновление INFO не отправляет firmware query. В файле есть версия приложения,
время, profile, полный/partial статус и записи:

- VID/PID, USB bcdDevice (отдельно от firmware), Usage/Page и реальный размер
  FeatureReport для подходящих ITE-интерфейсов;
- сырой ответ80H, четыре firmware-байта, строка версии вHEX и decimal;
- ForceBase/ForceLogo/ForceHinge, а также Forcenewlogosupport/MBlogoSupport
  из64- и32-битных представлений точного HKLM OEM RGBKeyboard key;
- missing/wrong_type/access error с Win32, null для неизвестного, и ошибки
  каждого интерфейса, включая blocked OEM/query/invalid reply;
- начало запроса до потенциально зависающего I/O, чтобы partial/timeout не
  выдавался за отсутствие устройства или версию0.0.0.0.

Helper отправляет только фиксированный80H: SetFeature запрашивает версию,
GetFeature получает ответ.08/14/1A и другие эффекты/brightness/save, записи
реестра и EC в этом getter не вызываются. SMBIOS/BIOS и allowlist дескрипторов
проверяются, активный/неизвестный OEM owner блокирует query. Существующий
HID transport переиспользуется; named mutex сериализует request/reply с GUI.
Parent ограничивает процесс20с; child имеет15с/64кандидата. При запрете
`--no-ec` остаются OS/registry metadata, firmware не запрашивается. Для других
профилей нового HID query нет; Linux пока не имеет такого getter.

Если данные доступны, один полный архив содержит всё для сопоставления зон с
родным кодом, включая исходный EC73C изec.bin. Если firmware/registry недоступны,
причина остаётся явной: неизвестное значение не доказывает отсутствие логотипа.
OEM Force-флаг — конфигурация программы, не физический тест LED. Обычный режим
дампа с Default/Max-пробами FANs этим дополнением не меняется.

Ниже — описание исходной интеграции0.1.173.

24.10.2026. Заголовок единственной компактной карточки выбирает только зоны,
которые вернул действующий backend. Одна зона — обычный заголовок, несколько —
тот же заголовок со стрелкой и общим native popup. Высота карточки, остальные
карточки, шапка приложения, размеры кнопок и прокрутка не меняются.

Выбор зоны только показывает её настройки: никаких apply/save/restore команд.
Busy/read не разрешает переключить адресата незавершённой команды.
Клавиатура Wujie, GM6, GM5, L16 и Huawei сохраняет прежние обработчики,
SingleZone-преобразования, Cycle6 и lifecycle. Новый Windows ITE backend выбирается
только для exact HYDROC. Linux использует тот же заголовок/геометрию и пока не
получает дополнительные зоны: рабочего HID/EC writer там нет.

## Что найдено в дампе HYDROC

Архив `Users_logs/logs_HYDROC-16.zip`, версия приложения0.1.69,19.10.2026:
полный4096-byte EC, BIOS N.1.13ELU11, Project26, platform13, EC73C=24,
RAM1.5 capability78E.6. Установленный11.2.18.347 SYS имеет собственный pin
e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2.
Inventory содержит обычные USB Input Device, но не VID/PID/Usage/FeatureLength,
ITE firmware80 или ForceBase/ForceLogo. Наличие полосы/логотипа из этих строк
не выводится. Исходный архив сохранён; новый опрос железа помощником не запускался.

## Windows ITE

Собственный ControlCenter5.23.51.56, восстановленный readable-csharp:

- LM_ITE_RGB4115–4299: дескриптор, FW80, EC73C определяют keyboard type.
- Для данного backend разрешена ветвь pageFF03/usage1/FWmajor34/EC73C24,
  MEZone_3p1nd_101. Другие поколения требуют собственных calibration/static-map
  codec и не получают guessed EC fallback.
- LM_ITE_RGB4382–4466: PID7000/7001,pageFF03,usage2 — lightbar4 endpoint.
- GetDeviceSupport4863–5070: тип lightbar сам по себе не доказывает наличие
  платы логотипа. Сопоставляются собственные OEM DWORD ForceBase/ForceLogo/
  ForceHinge=1 и реальный уникальный HID endpoint. Неизвестные/несколько
  конкурирующих endpoints отвергаются. ForceNewLogo не подменяет ForceLogo.
- Команды08/14: runtime Save=0. Контролы keyboard2/bar34/logo35/hinge33;
  RGB bank0/2/3/1. Глобальные выключения01/05 не используются.
- Яркость keyboard0/8/22/36/50, lightbar4: 0/25/50/75/100; speed10/7/5/3/1.
  Направления по Translate_ITE_DirectionValue: RightLeft2,LeftRight1.
- Моно1,пульсация2,волна3,радуга5. Скорость и направление в компактном меню
  «Эффекты…». Музыка/буквы/поклавишные host-анимации и welcome/flash не
  объявляются реализованными.1A — welcome timeout, не таймер бездействия.

Каждая запись повторно проверяет SMBIOS/BIOS, драйвер, Project26/P13/78E.6,
OEM ownership, descriptor и FW. Длина полного FeatureReport берётся из caps;
никакого предположения9/65bytes. Ответ80 проверяет reportID/command; это query,
не измерение цвета или физической яркости. Во время работы OEM запросы/записи
не выполняются. Помощник не запускал приложение/OEM/EC/HID.

Настройки `settings/hydroc16g2-lighting-{keyboard,lightbar,logo,hinge}.ini`
раздельны, атомарно сохраняются общим savePortableText после принятия runtime
HID команд и включаются в диагностический settings manifest. Чтение файла,
выбор зоны, startup/resume сами их не применяют. Успех SetFeature означает
приём команды транспортом, а не доказанный физический эффект. UI показывает
сохранённую конфигурацию; отсутствующая конфигурация не подсвечивает выдуманный
уровень/цвет. RGB readback в OEM88 отсутствует, полноценный rollback не заявлен.

## HYDROC FAN/CPU

Собственная платформа13 сохраняется. HYDROC допущен к общему typed RAM1.5
контроллеру со своими именами файлов и точным BIOS/driver/project guard.
При подключённом LC (7C8.5 AND7C7.1) FAN/PL записи запрещены. CPU PL4 использует
собственный double codec727.7; ручные PL дополнительно ограничены свежими
пределами собственного Turbo. Native GM6 factory curves на HYDROC не применяются.
Диагностические Default/Max/factory используют тот же controller gate, остановку
online и восстановление; factory mailbox ожидает Project26. Батарея и её
экспериментальный процентный контракт не расширены. EC SingleZone не включён.

Физическая работа новых HYDROC команд требует проверки владельцем на HYDROC;
сборка и проверки пакета таким подтверждением не являются.

## Pulse palette: native EC and HID

SingleZone GM BreathingIndex1–4 — четыре аппаратных пресета7C5.low3, а не
произвольные RGB. Выбор использует существующий core pulse setter с fresh guard,
masked readback и rollback, сохраняя FAN Respective7C5.7. GM UI подписывает
неизмеренные физические цвета номерами1–4; проверенная Wujie карта не меняется.
HID Breathing(effect2) принимает RGB из конфигурации зоны; изменение RGB не
должно включать Mono(effect1). Ни таймер, ни яркость, ни другая зона не меняются.
