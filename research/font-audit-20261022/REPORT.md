# Ревизия шрифтов Windows/Linux,22.10.2026

## Область и найденные пути

- Layout.h выбирал Segoe UI/Variable/MDL2 при отключённом EmbeddedFont или
  неудачной регистрации TTF; MainWindow.cpp допускал такой fallback.
- Linux Ui.cpp выбирал gtk-font-name/Sans; main.cpp продолжал GUI при
  недоступных комплектных файлах. BIOS зависел от системного font-name cache.
- Windows OSD и кнопки главного окна использовали Segoe MDL2 PUA glyphs.
- Tray/BIOS context menus и bootstrap MessageBox использовали native текст.
- Штатный color chooser и новые numeric fields могли до consumer layout
  оставаться с системным шрифтом.

## Новая политика

Только Noto Sans SC для RU/EN/简体中文 и Noto Sans TC для繁體中文.
Это прежние проверенные TTF/OFL из portable/fonts, не установка в ОС.
Название «embedded» в настройках обозначает комплектный шрифт; TTF в этой
правке не переносились из portable-файлов в RCDATA исполняемого файла.
Старый EmbeddedFont=0 читается совместимо, но не включает системный режим.
Если комплект отсутствует/не регистрируется, GUI не запускается с подменой.

Одна Windows private registration обслуживает основной UI и bootstrap dialogs.
Все createUiFont роли выбирают комплектное семейство. Shared ControlTheme
задаёт текст popup menus и color chooser; RegionTooltips уже получал body.
NumericFieldTheme назначает комплектный Caption при создании; потребитель
по-прежнему выбирает размер/роль. Размеры FANs/SETs и аппаратные команды не менялись.

Linux Pango получает отдельную Fontconfig collection только с SC/TC.
CSS всех окон/дочерних widgets использует выбранное семейство; gtk-font-name
больше не определяет семейство и не запускает перестроение метрик приложения.

Native non-client заголовки системных окон, UAC, shell/portal dialogs и desktop
notifications внешних процессов принадлежат ОС; ревизия относится к renderer,
контролам и меню самого приложения. Их шрифты не считаются доказанными
изменёнными системными настройками. Изменений системных шрифтов не выполнялось.

## Векторные значки и DPI

Общая OsdIcon.h заменяет PUA и GTK-image внутри OSD на17 векторных иконок.
Исходная сетка32×32 масштабируется в прежние35DIP внутри прежней области.
GDI/Cairo используют одинаковые линии, прямоугольники и овалы. Windows chrome
кнопки рисуются геометрией без шрифтов. Толщина и координаты масштабируются;
tray hidden owner перемещается на монитор anchor до измерения меню.
Bootstrap dialogs учитывают системный DPI; color chooser обновляет шрифт по DPI.

## Проверки

Никакие MagicControl GUI/EC/backend/драйверы для проверок не запускались.

Изолированный tests/offline/test_ui_fonts.cpp использует настоящий Layout.h
font factory и общий OsdIcon.h, регистрирует приватные TTF и рисует только DIB:

- 4 языка ×21 роль ×4 DPI96/144/192/240 =336 проверок фактического GetTextFace;
- 6216 проверок глифов полного каталога переводов через GetGlyphIndicesW,
  без разрешения замены отсутствующего глифа;
- 17 иконок ×4 DPI =68 проверок координат и offscreen render;
- failures=0. Листы100%/200% просмотрены, значки не обрезаны.

Изолированный tests/offline/test_ui_fonts_linux.cpp использует Fontconfig/Pango,
без GTK-window или прикладной библиотеки:

- 16 сочетаний языка/DPI,16456 shaped runs полного каталога;
- точный FC_FILE каждого run соответствует выбранному SC/TC в комплекте;
- unknown glyphs=0, failures=0.

Это не доказывает отсутствие всех возможных glyph fallback для произвольных
названий чужого оборудования/файлов, которых нет в каталоге четырёх языков.
Реальный multi-monitor transition и live menu navigation не проверялись.
Статический поиск в src/gui не нашёл прежних Segoe/Tahoma/Arial/Sans11,
gtk-font-name, MessageBoxW и stock-font selection. git diff --check пройден.

## Сборка

Общая Windows/Linux сборка0.1.133 выполнена через Tools/Build-All.ps1;
рабочие portable обеих ОС обновлены с проверкой SHA256. Первоначальные
ошибки компиляции menu adapter исправлены до завершения. Предупреждения
в существующих GPU/GM6/локализации остаются. GitHub-архивы не менялись.
Личные INI/CSV/логи не включались в коммит.

Уточнение0.1.134: у owner-draw popup separator сохраняется исходный
MFT_SEPARATOR, чтобы он оставался невыбираемой строкой. Дополнительная
изолированная проверка использует невидимый message-only STATIC owner:
проверяет separator flag, native measure и восстановление item type/data.
Она не открывает меню или приложение и не обращается к аппаратным API.
Целевые CTest ui_fonts_offline_audit и ui_fonts_linux_offline_audit прошли
на обеих ОС; остальные тесты/аппаратные сценарии этим запуском не выполнялись.

Финальная общая сборка0.1.134 завершена, обе рабочие portable сверены:

- Windows GUI SHA256 DD7F1890857D0E797902EFDC3214AF7DD2D180E086E9D038A8CC670BE6256C27
- Linux GUI SHA256 810DB34909E74E3E55D5A4E2638ED6ED05A3A2AC126EC6D9DCD238D811BBAA72

Downloads/MagicControl обновлён до0.1.134; EXE совпадает с Windows portable,
проверены TTF/OFL комплекта и неизменность хэшей INI/CSV. Приложение не
запускалось помощником, журналы не редактировались.
