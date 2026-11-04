# VBIOS collector0.1.194: сборка и файловая проверка

Один createVbiosRomDump подключён к Windows/Linux INFO и mctl. Два сохранения,
строгая OS/list PCI binding, повторные identity checks, nonempty size/SHA,
PCI ROM chain, канонический file hash, полные raw/events/manifest и partial
snapshots при ошибке. Чужой preview не даёт доступа, Unknown не создаёт пустые
GPU tabs,14 каталоговых NVIDIA платформ имеют соответствующую вкладку.

Windows использует новый SHA-pinned NvFlash RCDATA group и прежний vendor
process runner AFU/FPT; legacy BIOS selection/capture body/validator сохранены.
Linux использует свой SHA-pinned ELF, root-owned cache и native child без shell.
Фактические результаты, не проверявшиеся на железе:

- Build-All завершил общий0.1.194: обе GUI и обе CLI обновлены в рабочие portable.
-552 application inputs совпали с build receipt.
- Все4 бинарника побайтно совпали с build результатами, version strings/PE
  subsystem GUI2/CLI3/asInvoker проверены чтением файлов.
- Все47 embedded payload в каждом Windows EXE совпали с каталогом SHA/размеров.
- Linux staged NVFlash15909600байт совпал с pinned SHA; notices обеих ОС
  побайтно совпали с источниками.
- Legacy createPlatformBiosRomDump body и BiosImageValidation.h побайтно
  совпали с0.1.193. Старые AFU /O/FPT -I/-d маршруты не переписаны.
- Не изменены Geometry.h, core EC/protocol writers, FAN/APU/battery policy,
  user settings или GitHub archives.

Receipt: [verification.json](verification.json), file-only checker рядом.
Тесты, GUI/CLI, NVFlash, driver и аппаратные getters/setters не запускались.
Реальная совместимость read/save всех RTX40/50 и vendor side effects остаются
непроверенными. Источник и частичное статическое исследование:
[NVFlash integration](../nvflash-reader-20261025/INTEGRATION.md).
