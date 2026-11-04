# Доставка MagicControl 0.1.129

Общий Release Windows/Linux завершён21.10.2026, обе рабочие portable обновлены.
Четыре GUI/CLI файла совпадают с build по SHA; Windows version resources=0.1.129.
Подробные хэши/границы: `verification-0.1.129.json` (воспроизводит `verify.py`).

- GM6: собственная EC-батарея и каталог,63 диагностические строки, чистый OS
  источник отдельно, общий journal изменений/процентов/10с. Три fixture из архива.
- CPU GM6: Auto для нулевых overrides, источники Package RAPL и PL1/PL2/PL4;
  существующий CPU writer не изменён, Auto вызывает его с0/0/0.
- Общая FANs GPU-карточка: максимальная доступная нагрузка вместо display route.
- Windows5/5 и Linux4/4 offline suites,264506 проверок battery contract,
  80 сценариев/20394 проверки карточек,61 изолированная native control проверка,
  74 проверки парных log slots. JUnit сохранён рядом.
- Сверены468 application inputs,56 embedded payloads GUI/CLI,29 исходных runtime
  файлов; сохранены407 личных/runtime файлов,11 архивированных evidence файлов,
  24 защищённых исходника и.gitattributes. Встроенные drivers/PawnIO не вынесены.

Приложение/OEM/драйверы/EC/SMBus не запускались. Аппаратная отсечка зарядки,
CPU enforcement, графический результат на экране GM6 требуют проверки владельцем.
GitHub пакеты/публикация не выполнялись. Персональные настройки/логи в коммит
не входят; baseline содержит только их пути/хэши.
