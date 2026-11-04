# WUJIE Custom: убрать остаточный максимум45Вт на батарее

Причина:0.1.203/204 подняли AC input до255, но оставили supply-linked пределы
STAPM40/Slow40/Fast45 в редакторе и applyApuAllowedLimits. Пользователь подтвердил
работу от батареи. Снимок17:51 содержит текущие783/784/785=5/10/45.

Правка только точного WUJIE пути:

- Редактор Custom всегда5–255, не зависит от GetSystemPowerStatus.
- Draft не переписывается при отключении БП, focus commit не срезает до40/45.
- Ручной apply, Custom1/2/3, CLI power custom и восстановление Mode4/legacy
  Manual используют общий applyApuCustomLimits без DC-clamp.
- Setter applyManualPowerLimits остаётся тем же: exact-board capability,
  direct PL4 guard, byte-range validation, readback и rollback не ослаблены.
- Фиксированные Economy/Office/Gaming/Turbo сохраняют прежнюю политику питания.
  Другие модели и Linux hardware writers не расширены.
- Серые поля на скриншоте — закрытый редактор: выбранный сохранённый Custom
  не равнозначен режиму ввода. Нажатие Custom открывает редактор, Esc отменяет.

Введённые данные ещё не означают исполненную SMU мощность. Ни приложение,
ни EC/SMU действия при исправлении не запускаются. Проверка: compile обеих ОС,
статический аудит всех Custom/preset/restore call sites, portable versions/SHA.
