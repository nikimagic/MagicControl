#pragma once
#include "Fonts.h"
#include "TabLabels.h"
#include <atomic>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <initializer_list>
#include <utility>
#include <cctype>
#include <unordered_map>
namespace mc::gui::ui {
inline std::atomic<unsigned> activeLanguage{};
inline unsigned languageIndex(){return activeLanguage.load();}
inline void setLanguage(unsigned value){activeLanguage.store(value<4?value:0);}
struct Entry {std::string_view russian,english,simplified,traditional;};
inline const std::vector<Entry>& translations(){
    static const auto catalog=[] {std::vector<Entry> values{
    Entry{"Режим зарядки","Charge mode","充电模式","充電模式"},
    Entry{"Батарея · режим зарядки","Battery · charge mode","电池 · 充电模式","電池 · 充電模式"},
    Entry{"Запрос BMS: ток / напряжение","BMS request: current / voltage","BMS 请求：电流 / 电压","BMS 請求：電流 / 電壓"},
    Entry{"Конфликт режимов","Conflicting modes","模式冲突","模式衝突"},
    Entry{"Запрос BMS — это запрошенные ток и напряжение зарядки, а не измеренные значения на батарее.","The BMS request is the requested charging current and voltage, not measured battery values.","BMS 请求是充电电流和电压的请求值，并非电池实测值。","BMS 請求是充電電流和電壓的請求值，並非電池實測值。"},
    Entry{"Custom: значение вне диапазона WMI этого устройства. Проверьте пределы CPU, GPU, Tau и температур.","Custom: value outside this device's WMI range. Check CPU/GPU, Tau and temperature limits.","Custom：数值超出此设备的 WMI 范围。请检查 CPU/GPU、Tau 和温度限制。","Custom：數值超出此裝置的 WMI 範圍。請檢查 CPU/GPU、Tau 和溫度限制。"},
    Entry{"Темп. CPU","CPU Temp","CPU 温度","CPU 溫度"},
    Entry{"Темп. GPU","GPU Temp","GPU 温度","GPU 溫度"},
    Entry{"Смещение мощности","Power Offset","功率偏移","功率偏移"},
    Entry{"Dynamic Boost","Dynamic Boost","Dynamic Boost","Dynamic Boost"},
    Entry{"Cross Loading, Вт ","Cross Loading, W ","Cross Loading, 瓦 ","Cross Loading, 瓦 "},
    Entry{"Custom: отредактируйте параметры и нажмите Custom ещё раз для применения.","Custom: edit the settings and click Custom again to apply.","Custom：编辑设置后再次点击 Custom 应用。","Custom：編輯設定後再次點擊 Custom 套用。"},
    Entry{"Применяю Custom через ядро…","Applying Custom through the core…","正在通过核心应用 Custom…","正在透過核心套用 Custom…"},
    Entry{"Параметры подтверждены аппаратным чтением. Текущие вентиляторы сохранены.","Settings confirmed by hardware readback. Current fans retained.","设置已通过硬件回读确认。保留当前风扇设置。","設定已透過硬體回讀確認。保留目前風扇設定。"},
    Entry{"{source} {temperature} °C · Запрос {request} · Факт {actual} RPM","{source} {temperature} °C · Request {request} · Actual {actual} RPM","{source} {temperature} °C · 请求 {request} · 实际 {actual} RPM","{source} {temperature} °C · 請求 {request} · 實際 {actual} RPM"},
    Entry{"База EC · {source} · ступень {stage}/9","EC base · {source} · stage {stage}/9","EC 基准 · {source} · 档位 {stage}/9","EC 基準 · {source} · 檔位 {stage}/9"},
    Entry{"задержка EC","EC delay","EC 延迟","EC 延遲"},
    Entry{"EC CPU {temperature} °C · Запрос {request}% · Факт {actual}%","EC CPU {temperature} °C · Request {request}% · Actual {actual}%","EC CPU {temperature} °C · 请求 {request}% · 实际 {actual}%","EC CPU {temperature} °C · 請求 {request}% · 實際 {actual}%"},
    Entry{"EC CPU {temperature} °C · Запрос≈{request}% · Факт {actual}%","EC CPU {temperature} °C · Request≈{request}% · Actual {actual}%","EC CPU {temperature} °C · 请求≈{request}% · 实际 {actual}%","EC CPU {temperature} °C · 請求≈{request}% · 實際 {actual}%"},
    Entry{"{source} {temperature} °C · Запрос {request}% · Факт {actual}%","{source} {temperature} °C · Request {request}% · Actual {actual}%","{source} {temperature} °C · 请求 {request}% · 实际 {actual}%","{source} {temperature} °C · 請求 {request}% · 實際 {actual}%"},
    Entry{"{source} {temperature} °C · Запрос≈{request}% · Факт {actual}%","{source} {temperature} °C · Request≈{request}% · Actual {actual}%","{source} {temperature} °C · 请求≈{request}% · 实际 {actual}%","{source} {temperature} °C · 請求≈{request}% · 實際 {actual}%"},
    Entry{"{source} {temperature} °C · Запрос {request}% (база) · Факт {actual}%","{source} {temperature} °C · Request {request}% (base) · Actual {actual}%","{source} {temperature} °C · 请求 {request}%（基准）· 实际 {actual}%","{source} {temperature} °C · 請求 {request}%（基準）· 實際 {actual}%"},
    Entry{"Образ EC 1.12 · базовая кривая","EC 1.12 image · base curve","EC 1.12 映像 · 基准曲线","EC 1.12 映像 · 基準曲線"},
    Entry{"База EC · пуск {start} °C · стоп <{stop} °C","EC base · start {start} °C · stop <{stop} °C","EC 基准 · 启动 {start} °C · 停止 <{stop} °C","EC 基準 · 啟動 {start} °C · 停止 <{stop} °C"},
    Entry{"≈ расчёт","≈ derived","≈ 计算值","≈ 計算值"},
    Entry{"последнее чтение","last reading","上次读取","上次讀取"},
    Entry{"Default · управляющая температура EC недоступна","Default · EC control temperature unavailable","Default · EC 控制温度不可用","Default · EC 控制溫度不可用"},
    Entry{"Выключение подсветки через","Backlight off in","背光关闭倒计时","背光關閉倒數"},
    Entry{"Отключено","Disabled","已停用","已停用"},
    Entry{"Подсветка полосы","Lightbar lighting","灯带背光","燈帶背光"},
    Entry{"Подсветка логотипа","Logo lighting","标志背光","標誌背光"},
    Entry{"Подсветка шарнира","Hinge lighting","转轴背光","轉軸背光"},
    Entry{"Эффекты…","Effects…","灯效…","燈效…"},
    Entry{"Волна","Wave","波浪","波浪"},
    Entry{"Неон","Neon","霓虹","霓虹"},
    Entry{"Рябь","Ripple","涟漪","漣漪"},
    Entry{"Скорость","Speed","速度","速度"},
    Entry{"Направление","Direction","方向","方向"},
    Entry{"Влево","Left","向左","向左"},
    Entry{"Вправо","Right","向右","向右"},
    Entry{"Quiet","Quiet","静音","靜音"},
    Entry{"Balanced","Balanced","均衡","均衡"},
    Entry{"Performance","Performance","性能","效能"},
    Entry{"Обход","Bypass","绕过","繞過"},
    Entry{"Батарея: {mode}","Battery: {mode}","电池：{mode}","電池：{mode}"},
    Entry{"Режим батареи подтверждён чтением EC.","Battery mode confirmed by EC readback.","电池模式已通过 EC 回读确认。","電池模式已透過 EC 回讀確認。"},
    Entry{"Платформа и ROM ID подтверждены чтением EC.","Platform and ROM ID confirmed by EC readback.","平台及 ROM ID 已通过 EC 回读确认。","平台及 ROM ID 已透過 EC 回讀確認。"},
    Entry{"Цикл 6 запущен; смену цветов выполняет программа.","Cycle6 started; the application changes the colours.","Cycle6 已启动；由应用程序切换颜色。","Cycle6 已啟動；由應用程式切換顏色。"},
    Entry{"Состояние недоступно","State unavailable","状态不可用","狀態無法取得"},
    Entry{"Клавиатура","Keyboard","键盘","鍵盤"},
    Entry{"Полоса","Lightbar","灯带","燈帶"},
    Entry{"Логотип","Logo","标志","標誌"},
    Entry{"Шарнир","Hinge","转轴","轉軸"},
    Entry{"Событие аппаратной клавиши","Hardware key event","硬件按键事件","硬體按鍵事件"},
    Entry{"Скорость {number}","Speed {number}","速度 {number}","速度 {number}"},
    Entry{"Верхний предел подтверждён чтением.","Upper limit confirmed by readback.","上限已通过回读确认。","上限已透過回讀確認。"},
    Entry{"Лимит подтверждён чтением драйвера GPU.","Limit confirmed by GPU driver readback.","限制已通过 GPU 驱动回读确认。","限制已透過 GPU 驅動程式回讀確認。"},
    Entry{"Профиль изменён; результат прежней команды не применяется.","Profile changed; the previous command result is not applied.","配置已更改；不应用上一条命令的结果。","設定檔已變更；不套用上一個命令的結果。"},
    Entry{"Manual online включён; температуры CPU/GPU, защита85°C.","Manual online enabled; CPU/GPU temperatures,85°C protection.","Manual online 已启用；CPU/GPU 温度，85°C 保护。","Manual online 已啟用；CPU/GPU 溫度，85°C 保護。"},
    Entry{"STAPM, Вт ","STAPM, W ","STAPM, 瓦 ","STAPM, 瓦 "},
    Entry{"Slow PPT, Вт ","Slow PPT, W ","Slow PPT, 瓦 ","Slow PPT, 瓦 "},
    Entry{"Fast PPT, Вт ","Fast PPT, W ","Fast PPT, 瓦 ","Fast PPT, 瓦 "},
    Entry{"Лимиты: EC 46A/46B/46F","Limits: EC 46A/46B/46F","限制：EC 46A/46B/46F","限制：EC 46A/46B/46F"},
    Entry{"сверка SMU Fast/Slow/STAPM: {value} Вт","SMU check Fast/Slow/STAPM: {value} W","SMU 对照 Fast/Slow/STAPM：{value} 瓦","SMU 對照 Fast/Slow/STAPM：{value} 瓦"},
    Entry{"SMU: последнее показание","SMU: last reading","SMU：上次读数","SMU：上次讀數"},
    Entry{"AMD XRT: Estimated Power=N/A; драйвер не предоставил мощность NPU","AMD XRT: Estimated Power=N/A; the driver did not provide NPU power","AMD XRT：Estimated Power=N/A；驱动未提供 NPU 功率","AMD XRT：Estimated Power=N/A；驅動未提供 NPU 功率"},
    Entry{"EC: нет свежего результата","EC: no fresh result","EC：暂无最新结果","EC：暫無最新結果"},
    Entry{"Запрос записан. Контролы показывают итоговые лимиты EC; SMU — для сверки.","Request written. Fields show accepted EC limits; SMU is an independent comparison.","请求已写入。字段显示 EC 接受的限制；SMU 用于独立对照。","請求已寫入。欄位顯示 EC 接受的限制；SMU 用於獨立對照。"},
    Entry{"Логгер","Logger","日志记录器","記錄器"},
    Entry{"платформа 7","platform 7","平台 7","平台 7"},
    Entry{"исполнение не проверено","enforcement unverified","实际执行未验证","實際執行未驗證"},
    Entry{"Батарея: платформа 7, порог записан; физическое ограничение не проверено.","Battery: platform 7, threshold written; physical enforcement unverified.","电池：平台 7，阈值已写入；实际限制效果尚未验证。","電池：平台 7，閾值已寫入；實際限制效果尚未驗證。"},
    Entry{"Диапазон байта: 0x00–0xFF (0–255). Поддержка прошивкой зависит от адреса.","Byte range: 0x00–0xFF (0–255). Firmware support depends on the address.","字节范围：0x00–0xFF（0–255）。固件支持取决于地址。","位元組範圍：0x00–0xFF（0–255）。韌體支援取決於位址。"},
    Entry{"Известные значения:","Known values:","已知值：","已知值："},
    Entry{"0x04 (4) — платформа в последовательности ограничения заряда; также меняет лимиты APU.","0x04 (4) — platform used in the charge-limit sequence; also changes APU limits.","0x04（4）— 充电限制流程使用的平台值；也会改变 APU 限制。","0x04（4）— 充電限制流程使用的平台值；也會改變 APU 限制。"},
    Entry{"0x07 (7) — другая платформа: штатные профили APU на батарее имеют разные лимиты. Прочие значения не расшифрованы.","0x07 (7) — another platform: stock APU profiles have different limits on battery. Other values are undecoded.","0x07（7）— 另一平台值：电池供电时各默认 APU 模式的限制不同。其他值尚未解析。","0x07（7）— 另一平台值：電池供電時各預設 APU 模式的限制不同。其他值尚未解析。"},
    Entry{"b0–b6: 0x00 = 100%, 0x55 = 85%, 0x5F = 95%; b7 сохраняется.","b0–b6: 0x00 = 100%, 0x55 = 85%, 0x5F = 95%; b7 is preserved.","b0–b6：0x00 = 100%，0x55 = 85%，0x5F = 95%；保留 b7。","b0–b6：0x00 = 100%，0x55 = 85%，0x5F = 95%；保留 b7。"},
    Entry{"Это настройка порога, а не измеренный заряд и не гарантия физической отсечки.","This configures a threshold; it is not measured charge or proof of a physical cutoff.","这是阈值设置，并非实测电量，也不保证物理停止充电。","這是閾值設定，並非實測電量，也不保證實際停止充電。"},
    Entry{"b0–b6: заданный процент нижнего порога; b7 сохраняется.","b0–b6: configured lower percentage threshold; b7 is preserved.","b0–b6：配置的电量下限百分比；保留 b7。","b0–b6：設定的電量下限百分比；保留 b7。"},
    Entry{"Физическое действие нижнего порога пока не подтверждено.","Physical enforcement of the lower threshold is not yet confirmed.","尚未确认此下限的实际执行效果。","尚未確認此下限的實際執行效果。"},
    Entry{"Маски: 0x01 — управление приложения; 0x08 — reactive; 0x20 — fan abnormal (OEM).","Masks: 0x01 — application control; 0x08 — reactive; 0x20 — fan abnormal (OEM).","位掩码：0x01 — 应用控制；0x08 — reactive；0x20 — 风扇异常（OEM）。","位元遮罩：0x01 — 應用程式控制；0x08 — reactive；0x20 — 風扇異常（OEM）。"},
    Entry{"Это отдельные биты; прочие биты не расшифрованы, bit7 сохраняется.","These are individual bits; other bits are undecoded and bit7 is preserved.","这些是独立位；其他位尚未解析，保留 bit7。","這些是獨立位元；其他位元尚未解析，保留 bit7。"},
    Entry{"Расшифровка значений для этого адреса и модели отсутствует.","Values for this address and model have not been decoded.","此型号的该地址尚无已解析值。","此機型的該位址尚無已解析值。"},
    Entry{"Введите адрес и нажмите Enter","Enter an address and press Enter","输入地址后按 Enter","輸入位址後按 Enter"},
    Entry{"Неверный HEX адрес или байт","Invalid HEX address or byte","十六进制地址或字节无效","十六進位位址或位元組無效"},
    Entry{"Сначала перечитайте выбранный адрес клавишей Enter","Read the selected address with Enter first","请先按 Enter 读取所选地址","請先按 Enter 讀取所選位址"},
    Entry{"Читаю байт EC…","Reading EC byte…","正在读取 EC 字节…","正在讀取 EC 位元組…"},
    Entry{"Записываю байт EC…","Writing EC byte…","正在写入 EC 字节…","正在寫入 EC 位元組…"},
    Entry{"Байт EC прочитан.","EC byte read.","EC 字节已读取。","EC 位元組已讀取。"},
    Entry{"Байт записан; чтение обратно совпало.","Byte written; readback matched.","字节已写入，回读匹配。","位元組已寫入，讀回相符。"},
    Entry{"Raw EC для этой модели/ОС недоступен","Raw EC is unavailable for this model/OS","此型号或系统不支持原始 EC 访问","此機型或系統不支援原始 EC 存取"},
    Entry{"Назначение адреса для этой платы не подтверждено.","The address meaning is unverified for this board.","此主板上该地址的含义尚未确认。","此主機板上該位址的含義尚未確認。"},
    Entry{"Возобновить заряд ниже","Resume charging below","低于此电量恢复充电","低於此電量恢復充電"},
    Entry{"Применяю нижний порог…","Applying the lower threshold…","正在应用充电下限…","正在套用充電下限…"},
    Entry{"Нижний порог записан и сохранён.","Lower threshold written and saved.","充电下限已写入并保存。","充電下限已寫入並儲存。"},
    Entry{"Неверный нижний порог зарядки","Invalid lower charge threshold","充电下限无效","充電下限無效"},
    Entry{"Нижний порог должен быть ниже верхнего","The lower threshold must be below the upper threshold","下限必须低于上限","下限必須低於上限"},
    Entry{"Сначала выберите верхний предел зарядки","Select an upper charge limit first","请先选择充电上限","請先選擇充電上限"},
    Entry{"Батарея · верхний предел зарядки","Battery charge limit","电池充电上限","電池充電上限"},
    Entry{"Производительность Intel CPU","Intel CPU performance","Intel CPU 性能","Intel CPU 效能"},
    Entry{"Производительность APU","APU performance","APU 性能","APU 效能"},
    Entry{"Подсветка клавиатуры","Keyboard backlight","键盘背光","鍵盤背光"},
    Entry{"Цикл 6","6-color cycle","六色循环","六色循環"},
    Entry{"Каждые","Every","每隔","每隔"},
    Entry{"Цвета","Colors","颜色","顏色"},
    Entry{"BIOS · сведения о ноутбуке","BIOS · Laptop information","BIOS · 笔记本信息","BIOS · 筆記型電腦資訊"},
    Entry{"INFO · сведения о ноутбуке","INFO · Laptop information","INFO · 笔记本信息","INFO · 筆記型電腦資訊"},
    Entry{"Обновить сведения","Refresh information","刷新信息","更新資訊"},
    Entry{"Установить драйвер PawnIO","Install PawnIO driver","安装 PawnIO 驱动","安裝 PawnIO 驅動"},
    Entry{"Снять дамп","Collect diagnostic dump","收集诊断转储","收集診斷傾印"},
    Entry{"Снять дамп EC","Dump EC","导出 EC","匯出 EC"},
    Entry{"Снять дамп BIOS","Dump BIOS","导出 BIOS","匯出 BIOS"},
    Entry{"Снять дамп VBIOS","Dump VBIOS","导出 VBIOS","匯出 VBIOS"},
    Entry{"Снять полный EEPROM","Dump entire EEPROM","导出完整 EEPROM","匯出完整 EEPROM"},
    Entry{"Выберите, что прочитать с NVIDIA","Choose what to read from NVIDIA","选择 NVIDIA 读取内容","選擇 NVIDIA 讀取內容"},
    Entry{"NVIDIA: выберите обычный VBIOS или полный EEPROM. Для EEPROM нужна подтверждённая ёмкость чипа. Нужны права администратора/root.","NVIDIA: choose VBIOS or the entire EEPROM. EEPROM requires a confirmed chip capacity. Administrator/root required.","NVIDIA：选择 VBIOS 或完整 EEPROM。EEPROM 需要确认芯片容量。需要管理员/root 权限。","NVIDIA：選擇 VBIOS 或完整 EEPROM。EEPROM 需要確認晶片容量。需要系統管理員/root 權限。"},
    Entry{"NVIDIA: VBIOS и полный EEPROM, по два чтения. Ёмкость, SHA256 и raw compare; EEPROM до 16 МиБ. Нужны права администратора/root.","NVIDIA: VBIOS and entire EEPROM, two reads each. Capacity, SHA256 and raw compare; EEPROM up to16MiB. Administrator/root required.","NVIDIA：VBIOS 与完整 EEPROM，各读取两次。验证容量、SHA256 和原始比较；EEPROM 上限16MiB。需要管理员/root 权限。","NVIDIA：VBIOS 與完整 EEPROM，各讀取兩次。驗證容量、SHA256 和原始比較；EEPROM 上限16MiB。需要管理員/root 權限。"},
    Entry{"Отмена VBIOS","Cancel VBIOS dump","取消 VBIOS 导出","取消 VBIOS 匯出"},
    Entry{"Выберите NVIDIA для дампа VBIOS","Select NVIDIA GPU for VBIOS dump","选择要导出 VBIOS 的 NVIDIA 显卡","選擇要匯出 VBIOS 的 NVIDIA 顯示卡"},
    Entry{"Отмена BIOS","Cancel BIOS dump","取消 BIOS 导出","取消 BIOS 匯出"},
    Entry{"Копировать отчёт","Copy report","复制报告","複製報告"},
    Entry{"Быстрое переключение","Quick toggles","快捷开关","快速切換"},
    Entry{"Блокировка клавиши Windows","Windows key lock","锁定 Windows 键","鎖定 Windows 鍵"},
    Entry{"Отключить экранное меню","Disable on-screen display","禁用屏幕提示","停用螢幕提示"},
    Entry{"Блокировка клавиши Fn","Fn key lock","Fn 键锁定","Fn 鍵鎖定"},
    Entry{"Питание USB при выключении и гибернации","USB power while off or hibernating","关机和休眠时 USB 供电","關機和休眠時 USB 供電"},
    Entry{"Автовключение при подаче питания","Power on when AC is connected","接通电源时自动开机","接通電源時自動開機"},
    Entry{"Блокировка клавиши Copilot","Copilot key lock","锁定 Copilot 键","鎖定 Copilot 鍵"},
    Entry{"Блокировка действует, пока программа открыта; выбор сохраняется.","Blocking works while the app is open; the choice is saved.","应用运行时生效；选择会保存。","應用程式執行時生效；選擇會儲存。"},
    Entry{"Отключает наши уведомления OSD. Выбор сохраняется рядом с EXE.","Disables MagicControl OSD. The preference is saved beside the app.","禁用 MagicControl 屏幕提示；设置保存在程序旁。","停用 MagicControl 螢幕提示；設定儲存在程式旁。"},
    Entry{"Отключает наши уведомления OSD. Выбор сохраняется в settings.","Disables MagicControl OSD. The preference is saved in settings.","禁用 MagicControl 屏幕提示；设置保存在 settings 中。","停用 MagicControl 螢幕提示；設定儲存在 settings 中。"},
    Entry{"Отключает наши экранные уведомления. Выбор сохраняется в settings.","Disables our on-screen notifications. The preference is saved in settings.","禁用屏幕提示；设置保存在 settings 中。","停用螢幕提示；設定儲存在 settings 中。"},
    Entry{"Флаг EC. Автозапуск проверяется после перезагрузки и подключения адаптера.","EC flag. Check automatic startup after rebooting and connecting the adapter.","EC 标志。重启并连接电源适配器后检查自动开机。","EC 旗標。重新啟動並連接電源變壓器後檢查自動開機。"},
    Entry{"Флаг EC подтверждён. Автозапуск проверяется после перезагрузки.","EC flag confirmed. Check automatic startup after rebooting.","EC 标志已确认。重启后检查自动开机。","EC 旗標已確認。重新啟動後檢查自動開機。"},
    Entry{"EC не подтвердил переключение","The EC did not confirm the switch","EC 未确认切换","EC 未確認切換"},
    Entry{"Fn: итоговое состояние не подтверждено","Fn: final state was not confirmed","Fn：未确认最终状态","Fn：未確認最終狀態"},
    Entry{"Устройства Windows","Windows devices","Windows 设备","Windows 裝置"},
    Entry{"Текущее состояние Windows.","Current Windows state.","Windows 当前状态。","Windows 目前狀態。"},
    Entry{"Веб-камера","Webcam","网络摄像头","網路攝影機"},
    Entry{"Сенсорная панель","Touchpad","触摸板","觸控板"},
    Entry{"Состояния обновляются автоматически.","States refresh automatically.","状态自动更新。","狀態自動更新。"},
    Entry{"Экономичный","Power saving","节能","省電"},
    Entry{"Офисный","Office","办公","辦公"},
    Entry{"Сбалансированный","Balanced","平衡","平衡"},
    Entry{"Турбо","Turbo","极速","極速"},
    Entry{"Пульсация","Breathing","呼吸灯","呼吸燈"},
    Entry{"Мульти","Multicolor","多色","多色"},
    Entry{"Моно","Static","单色","單色"},
    Entry{"Свой цвет…","Custom color…","自定义颜色…","自訂顏色…"},
    Entry{"Свой цвет...","Custom color...","自定义颜色...","自訂顏色..."},
    Entry{"Авто Выкл","Auto off","自动关闭","自動關閉"},
    Entry{"Штатный режим","Default preset","默认预设","預設值"},
    Entry{"Штатный лимит","Default limit","默认功率限制","預設功率限制"},
    Entry{"Применить","Apply","应用","套用"},
    Entry{"Лимит мощности, Вт","Power limit, W","功率限制，W","功率限制，W"},
    Entry{"Лимит мощности, %","Power limit, %","功率限制，%","功率限制，%"},
    Entry{"Температура","Temperature","温度","溫度"},
    Entry{"Загрузка GPU","GPU usage","GPU 利用率","GPU 使用率"},
    Entry{"Занято VRAM","VRAM used","已用显存","已用顯示記憶體"},
    Entry{"Потребление","Power usage","功耗","耗電功率"},
    Entry{"CPU вентилятор","CPU fan","CPU 风扇","CPU 風扇"},
    Entry{"Системный вентилятор","System fan","系统风扇","系統風扇"},
    Entry{"Цель оборотов · CPU 36–100% · System 38–100%","RPM target · CPU36–100% · System38–100%","转速目标 · CPU36–100% · 系统38–100%","轉速目標 · CPU36–100% · 系統38–100%"},
    Entry{"HPBSD: применяю режим вентиляторов…","HPBSD: applying fan mode…","HPBSD：正在应用风扇模式…","HPBSD：正在套用風扇模式…"},
    Entry{"HPBSD: штатные таблицы восстановлены.","HPBSD: firmware fan tables restored.","HPBSD：已恢复原厂风扇表。","HPBSD：已還原原廠風扇表。"},
    Entry{"HPBSD: Default EC; штатное термоуправление CPU/System","HPBSD: Default EC; native CPU/system thermal control","HPBSD：Default EC；CPU/系统原厂温控","HPBSD：Default EC；CPU/系統原廠溫控"},
    Entry{"HPBSD: цели оборотов подтверждены; штатные высокотемпературные ступени сохранены","HPBSD: requested RPM targets confirmed; firmware hot-temperature rows retained","HPBSD：转速目标已确认；保留原厂高温档位","HPBSD：轉速目標已確認；保留原廠高溫檔位"},
    Entry{"HPBSD: незавершённая команда; выберите Default для восстановления","HPBSD: interrupted control; select Default for verified recovery","HPBSD：控制中断；请选择 Default 恢复","HPBSD：控制中斷；請選擇 Default 還原"},
    Entry{"HPBSD: внешняя таблица вентиляторов; управление недоступно","HPBSD: external fan table; control unavailable","HPBSD：外部风扇表；控制不可用","HPBSD：外部風扇表；控制不可用"},
    Entry{"HPBSD: цели оборотов подтверждены; верхние температурные ступени сохранены.","HPBSD: RPM targets confirmed; hot-temperature stages retained.","HPBSD：转速目标已确认，保留高温保护档位。","HPBSD：轉速目標已確認，保留高溫保護檔位。"},
    Entry{"Поддержка MINISFORUM","MINISFORUM support","MINISFORUM 支持","MINISFORUM 支援"},
    Entry{"GPU вентилятор","GPU fan","GPU 风扇","GPU 風扇"},
    Entry{"Вентилятор","Fan","风扇","風扇"},
    Entry{"Мощность CPU","CPU power","CPU 功耗","CPU 功耗"},
    Entry{"Мощность APU","APU power","APU 功耗","APU 功耗"},
    Entry{"Штатное управление EC","Firmware EC control","EC 自动控制","EC 自動控制"},
    Entry{"Не применена","Not applied","未应用","未套用"},
    Entry{"Применена","Applied","已应用","已套用"},
    Entry{"Нет подтверждения","Unconfirmed","未确认","未確認"},
    Entry{"Общая кривая EC","Shared EC curve","共用 EC 曲线","共用 EC 曲線"},
    Entry{"гистерезис OEM","OEM hysteresis","OEM 回差","OEM 遲滯"},
    Entry{"Задано","Configured","已设置","已設定"},
    Entry{"факт","actual","实际","實際"},
    Entry{"Память","Memory","内存","記憶體"},
    Entry{"Батарея","Battery","电池","電池"},
    Entry{"Диск","Disk","磁盘","磁碟"},
    Entry{"Не установлен","Not installed","未安装","未安裝"},
    Entry{"Температура недоступна","Temperature unavailable","温度不可用","溫度無法取得"},
    Entry{"От сети","On AC power","外接电源","外接電源"},
    Entry{"от сети","AC power","外接电源","外接電源"},
    Entry{"От батареи","On battery","电池供电","電池供電"},
    Entry{"от батареи","battery","电池供电","電池供電"},
    Entry{"Зарядка","Charging","充电","充電"},
    Entry{"Разряд","Discharging","放电","放電"},
    Entry{"Статус EC","EC status","EC 状态","EC 狀態"},
    Entry{"Направление тока","Current direction","电流方向","電流方向"},
    Entry{"Нулевой ток","Zero current","零电流","零電流"},
    Entry{"Покой","Idle","静止","靜止"},
    Entry{"Циклов","Cycles","循环次数","循環次數"},
    Entry{"Здоровье","Health","健康度","健康度"},
    Entry{"Питание","Power source","电源","電源"},
    Entry{"Порт зарядки","Charging port","充电接口","充電連接埠"},
    Entry{"БП EC ≈ {power} Вт · вход не определён","EC adapter ≈ {power} W · input unknown","EC 电源 ≈ {power} W · 输入未知","EC 電源 ≈ {power} W · 輸入未知"},
    Entry{"БП EC · мощность не определена","EC adapter · power unknown","EC 电源 · 功率未知","EC 電源 · 功率未知"},
    Entry{"Уровень","Level","等级","等級"},
    Entry{"Яркость","Brightness","亮度","亮度"},
    Entry{"Низкая","Low","低","低"},
    Entry{"Высокая","High","高","高"},
    Entry{"Выкл.","Off","关闭","關閉"},
    Entry{"Вкл.","On","开启","開啟"},
    Entry{"Включён","Enabled","已启用","已啟用"},
    Entry{"Выключен","Disabled","已禁用","已停用"},
    Entry{"Включено","Enabled","已启用","已啟用"},
    Entry{"Выключено","Disabled","已禁用","已停用"},
    Entry{"Цвет","Color","颜色","色彩"},
    Entry{"сек","sec","秒","秒"},
    Entry{"Длительно","Sustained","持续","持續"},
    Entry{"Средняя","Average","平均","平均"},
    Entry{"Пик","Peak","峰值","峰值"},
    Entry{"об/мин","RPM","转/分","轉/分"},
    Entry{"МГц","MHz","MHz","MHz"},
    Entry{"ГиБ","GiB","GiB","GiB"},
    Entry{"МиБ","MiB","MiB","MiB"},
    Entry{"мА","mA","mA","mA"},
    Entry{"Вт","W","W","W"},
    Entry{"мощность","power","功率","功率"},
    Entry{"Мощность","Power","功率","功率"},
    Entry{"Частота GPU","GPU clock","GPU 频率","GPU 時脈"},
    Entry{"Частота","Clock","频率","時脈"},
    Entry{"частота","clock","频率","時脈"},
    Entry{"Загрузка","Usage","利用率","使用率"},
    Entry{"Заряд","Charge","电量","電量"},
    Entry{"всего","total","总计","總計"},
    Entry{"свободно","free","可用","可用"},
    Entry{"память","memory","内存","記憶體"},
    Entry{"действующий предел","enforced limit","有效限制","有效限制"},
    Entry{"Диапазон","Range","范围","範圍"},
    Entry{"диапазон","range","范围","範圍"},
    Entry{"штатный","default","默认","預設"},
    Entry{"Драйвер","Driver","驱动","驅動"},
    Entry{"Лицензия","License","许可证","授權"},
    Entry{"Закрыть","Close","关闭","關閉"},
    Entry{"Проверить обновления","Check for updates","检查更新","檢查更新"},
    Entry{"Проверить","Check","检查","檢查"},
    Entry{"Обновить","Update","更新","更新"},
    Entry{"Загрузка…","Downloading…","正在下载…","正在下載…"},
    Entry{"Открыть","Open","打开","開啟"},
    Entry{"Выйти","Exit","退出","結束"},
    Entry{"Выход","Exit","退出","結束"},
    Entry{"Отмена","Cancel","取消","取消"},
    Entry{"Ошибка","Error","错误","錯誤"},
    Entry{"Поддержка","Support","支持","支援"},
    Entry{"Донаты","Donations","捐赠","贊助"},
    Entry{"Ноутбук","Laptop","笔记本","筆記型電腦"},
    Entry{"Ноутбуки","Laptops","笔记本电脑","筆記型電腦"},
    Entry{"Другая модель — просмотр","Other models open in preview","其他型号仅供预览","其他型號僅供預覽"},
    Entry{"Просмотр ноутбука","Laptop preview","笔记本预览","筆記型電腦預覽"},
    Entry{"Просмотр профиля · аппаратное управление отключено","Profile preview · hardware control disabled","配置预览 · 硬件控制已禁用","設定檔預覽 · 硬體控制已停用"},
    Entry{"Просмотр","Preview","预览","預覽"},
    Entry{"Пример · просмотр","Example · preview","示例 · 预览","範例 · 預覽"},
    Entry{"Нажмите, чтобы скопировать","Click to copy","点击复制","點擊複製"},
    Entry{"QR-код скопирован","QR image copied","二维码图片已复制","QR 碼圖片已複製"},
    Entry{"Адрес скопирован","Address copied","地址已复制","地址已複製"},
    Entry{"Скопировано","Copied","已复制","已複製"},
    Entry{"Модель из прошивки","Firmware model","固件型号","韌體型號"},
    Entry{"Производитель платы","Board manufacturer","主板制造商","主機板製造商"},
    Entry{"Производитель BIOS","BIOS vendor","BIOS 厂商","BIOS 廠商"},
    Entry{"Производитель матрицы","Panel manufacturer","面板制造商","面板製造商"},
    Entry{"Производитель","Manufacturer","制造商","製造商"},
    Entry{"Семейство","Family","系列","系列"},
    Entry{"Материнская плата","Motherboard","主板","主機板"},
    Entry{"Версия BIOS","BIOS version","BIOS 版本","BIOS 版本"},
    Entry{"Дата BIOS","BIOS date","BIOS 日期","BIOS 日期"},
    Entry{"Версия EC","EC version","EC 版本","EC 版本"},
    Entry{"Режим загрузки","Boot mode","启动模式","開機模式"},
    Entry{"Идентификатор системной прошивки UEFI","UEFI firmware identifier","UEFI 固件标识","UEFI 韌體識別碼"},
    Entry{"Версия SMBIOS","SMBIOS version","SMBIOS 版本","SMBIOS 版本"},
    Entry{"Батарея · ограничение по напряжению (штатный режим)","Battery · voltage limit (native mode)","电池 · 电压限制（原厂模式）","電池 · 電壓限制（原廠模式）"},
    Entry{"Батарея · ограничение по ёмкости","Battery · charge limit","电池 · 电量限制","電池 · 電量限制"},
    Entry{"Зарядное напряжение: данные не прочитаны","Charging voltage: data unavailable","充电电压：数据不可用","充電電壓：資料無法讀取"},
    Entry{"Запрос BMS: {base} мВ → цель EC: {target} мВ · снижение {delta} мВ","BMS request: {base} mV → EC target: {target} mV · reduction {delta} mV","BMS 请求：{base} mV → EC 目标：{target} mV · 降低 {delta} mV","BMS 請求：{base} mV → EC 目標：{target} mV · 降低 {delta} mV"},
    Entry{"Запрос BMS: {base} мВ · сохранённая цель EC: {target} мВ (внешнее питание не подтверждено)","BMS request: {base} mV · saved EC target: {target} mV (AC not confirmed)","BMS 请求：{base} mV · 已保存 EC 目标：{target} mV（未确认外部电源）","BMS 請求：{base} mV · 已儲存 EC 目標：{target} mV（未確認外部電源）"},
    Entry{"Сохранённые данные · ","Saved readings · ","已保存数据 · ","已儲存資料 · "},
    Entry{"Снижение: {delta} В · BMS {base} → цель EC {target} В","Reduction: {delta} V · BMS {base} → EC target {target} V","降低：{delta} V · BMS {base} → EC 目标 {target} V","降低：{delta} V · BMS {base} → EC 目標 {target} V"},
    Entry{"BMS {base} → цель EC {target} В","BMS {base} → EC target {target} V","BMS {base} → EC 目标 {target} V","BMS {base} → EC 目標 {target} V"},
    Entry{"Сохранённая разница: {delta} В · BMS {base} → цель EC {target} В","Saved difference: {delta} V · BMS {base} → EC target {target} V","已保存差值：{delta} V · BMS {base} → EC 目标 {target} V","已儲存差值：{delta} V · BMS {base} → EC 目標 {target} V"},
    Entry{"Project ID EC [740]","EC Project ID [740]","EC 项目 ID [740]","EC 專案 ID [740]"},
    Entry{"Платформа EC [7C3]","EC platform [7C3]","EC 平台 [7C3]","EC 平台 [7C3]"},
    Entry{"ROM ID EC [770–77F]","EC ROM ID [770–77F]","EC ROM ID [770–77F]","EC ROM ID [770–77F]"},
    Entry{"Дисплей","Display","显示器","顯示器"},
    Entry{"матрица","panel","面板","面板"},
    Entry{"Идентификатор панели","Panel identifier","面板标识","面板識別碼"},
    Entry{"Модель из EDID","EDID model","EDID 型号","EDID 型號"},
    Entry{"Код модели EDID","EDID model code","EDID 型号代码","EDID 型號代碼"},
    Entry{"Версия EDID","EDID version","EDID 版本","EDID 版本"},
    Entry{"Изготовление","Manufacture date","生产日期","製造日期"},
    Entry{"Глубина цвета EDID","EDID color depth","EDID 色深","EDID 色深"},
    Entry{"Сигнал EDID","EDID signal","EDID 信号","EDID 訊號"},
    Entry{"Гамма EDID","EDID gamma","EDID 伽马","EDID Gamma"},
    Entry{"Процессор","Processor","处理器","處理器"},
    Entry{"Оперативная память","System memory","系统内存","系統記憶體"},
    Entry{"Накопители","Storage","存储设备","儲存裝置"},
    Entry{"Модель","Model","型号","型號"},
    Entry{"Не сообщается","Not reported","未报告","未回報"},
    Entry{"Неизвестно","Unknown","未知","未知"},
    Entry{"Да","Yes","是","是"},
    Entry{"Нет","No","否","否"},
    Entry{"Сведения о ноутбуке","Laptop information","笔记本信息","筆記型電腦資訊"},
    Entry{"Обновление — только по кнопке.","Refresh is manual.","仅手动刷新。","僅手動更新。"},
    Entry{"Сведения обновлены и сохранены","Information refreshed and saved","信息已刷新并保存","資訊已更新並儲存"},
    Entry{"Сохранённые сведения от","Saved information from","已保存的信息，时间","已儲存的資訊，時間"},
    Entry{"Сохранённых сведений ещё нет. Нажмите «Обновить сведения».","No saved information. Click Refresh information.","暂无已保存的信息，请点击刷新信息。","尚無已儲存資訊，請點擊更新資訊。"},
    Entry{"Драйвер PawnIO уже установлен.","PawnIO driver is already installed.","PawnIO 驱动已安装。","PawnIO 驅動已安裝。"},
    Entry{"Дамп и диагностика сохранены","Dump and diagnostics saved","转储和诊断已保存","傾印與診斷已儲存"},
    Entry{"Изменение подтверждено EC.","Change confirmed by EC readback.","EC 读回已确认更改。","EC 讀回已確認變更。"},
    Entry{"Состояния EC обновлены.","EC states refreshed.","EC 状态已刷新。","EC 狀態已更新。"},
    Entry{"Читаю данные","Reading data","正在读取数据","正在讀取資料"},
    Entry{"EC недоступен.","EC unavailable.","EC 不可用。","EC 無法使用。"},
    Entry{"Аппаратные настройки отключены.","Hardware settings disabled.","硬件设置已禁用。","硬體設定已停用。"},
    Entry{"Управление недоступно","Control unavailable","控制不可用","控制無法使用"},
    Entry{"пока не подключено","not connected yet","尚未连接","尚未連接"},
    Entry{"Функция для","Feature for","功能对应","功能對應"},
    Entry{"пока не подключена.","not available yet.","尚不可用。","尚未提供。"},
    Entry{"только чтение","read only","只读","唯讀"},
    Entry{"чтение подтверждено","read confirmed","读取已确认","讀取已確認"},
    Entry{"реакция вентиляторов подтверждена пробой","fan response confirmed by probe","测试已确认风扇响应","測試已確認風扇反應"},
    Entry{"исполнение CPU PL не измерено","CPU PL enforcement not measured","未测量 CPU PL 执行效果","未量測 CPU PL 執行效果"},
    Entry{"аномалия драйвера","driver anomaly","驱动异常","驅動異常"},
    Entry{"Сеть · звук · камера · NPU","Network · audio · camera · NPU","网络 · 音频 · 摄像头 · NPU","網路 · 音效 · 攝影機 · NPU"},
    Entry{"Память, сообщённая WMI","Memory reported by WMI","WMI 报告的内存","WMI 回報的記憶體"},
    Entry{"Установлено модулей","Installed modules","已安装模块","已安裝模組"},
    Entry{"Всего","Total","总计","總計"},
    Entry{"Серийный номер","Serial number","序列号","序號"},
    Entry{"Объём","Capacity","容量","容量"},
    Entry{"Тип","Type","类型","類型"},
    Entry{"Формат","Form factor","规格","規格"},
    Entry{"Подключение","Connection","连接","連線"},
    Entry{"Состояние Windows","Windows state","Windows 状态","Windows 狀態"},
    Entry{"Система","System","系统","系統"},
    Entry{"Архитектура","Architecture","架构","架構"},
    Entry{"Последняя загрузка","Last boot","上次启动","上次開機"},
    Entry{"Зарядка остановлена","Charging stopped","充电已停止","充電已停止"},
    Entry{"Беспроводные устройства","Wireless devices","无线设备","無線裝置"},
    Entry{"Сохранить","Save","保存","儲存"},
    Entry{"Настройки","Settings","设置","設定"},
    #include "TranslationCatalog.inc"
    };std::stable_sort(values.begin(),values.end(),[](const auto& a,const auto& b){return a.russian.size()>b.russian.size();});return values;}();
    return catalog;
}
// Localize only complete known messages or reviewed structured display fields.
// Unknown sentences/vendor errors stay intact: never substitute single words
// inside an unknown phrase. Stored IDs, command strings and paths are untouched.
namespace detail {
inline std::string_view text(const Entry& e,unsigned language){
    return language==1?e.english:language==2?e.simplified:language==3?e.traditional:e.russian;
}
inline bool protectedName(std::string_view s){
    // Fixed product navigation bypasses every language catalog, even if a
    // future catalog accidentally adds an entry for one of these labels.
    return isFixedTabLabel(s)||s=="Unknown"||s=="Default"||s=="Manual"||s=="Max"||s=="Manual online"||s=="Manual1"||s=="Manual2"||s=="Manual3"||s=="Manual\n1"||s=="Manual\n2"||s=="Manual\n3"||
             s=="Minimal"||s=="Economy"||s=="Office"||s=="Gaming"||s=="Turbo"||s=="Custom"||s=="Auto"||s=="Balanced"||s=="Performance"||
           s=="FANs"||s=="SETs"||s=="KEYs"||s=="BIOS"||s=="CPU=GPU"||s=="CPU≠GPU";
}
inline bool path(std::string_view s){
    return (s.size()>2&&std::isalpha(static_cast<unsigned char>(s[0]))&&s[1]==':'&&(s[2]=='\\'||s[2]=='/'))||
        s.starts_with("\\\\")||s.starts_with("/home/")||s.starts_with("/sys/")||s.starts_with("/mnt/")||s.starts_with("/tmp/")||
        s.starts_with("PCI\\")||s.starts_with("USB\\")||s.starts_with("HID\\")||s.starts_with("ACPI\\")||
        s.starts_with("http://")||s.starts_with("https://");
}
inline const Entry* exact(std::string_view source){
    static const auto keys=[] {std::unordered_map<std::string_view,const Entry*> result;
        for(const auto& e:translations())result.try_emplace(e.russian,&e);return result;}();
    if(const auto found=keys.find(source);found!=keys.end())return found->second;
    // A few backends expose our own English errors. Exact aliases allow RU too;
    // opaque vendor sentences still have no match and retain their original text.
    static const auto aliases=[] {std::unordered_map<std::string_view,const Entry*> result;
        for(const auto& e:translations())result.try_emplace(e.english,&e);return result;}();
    if(const auto found=aliases.find(source);found!=aliases.end())return found->second;
    return nullptr;
}
using Argument=std::pair<std::string_view,std::string_view>;
inline std::string substitute(std::string_view pattern,const std::vector<Argument>& args){
    std::string result;result.reserve(pattern.size());
    for(std::size_t at=0;at<pattern.size();){
        if(pattern[at]=='{'){const auto end=pattern.find('}',at+1);if(end!=std::string_view::npos){
            const auto key=pattern.substr(at+1,end-at-1);bool found=false;
            for(const auto& a:args)if(a.first==key){result+=a.second;found=true;break;}
            if(found){at=end+1;continue;}
        }}result+=pattern[at++];
    }return result;
}
inline bool numeric(std::string_view s){
    if(s.empty())return false;bool digit=false;
    for(unsigned char c:s){if(c>='0'&&c<='9'){digit=true;continue;}
        if(c>=0x80||(!std::isspace(c)&&c!='.'&&c!=','&&c!='-'&&c!='+'&&c!='%'&&c!='/'&&c!=':'))return false;
    }return digit;
}
// Quantity admission is intentionally narrower than message rendering: an
// inferred quantity argument must be entirely a scalar plus one known unit,
// or a reviewed pair/range of such quantities. Never parse owners/IDs/errors.
inline bool quantity(std::string_view source,unsigned language,std::string& result,unsigned depth=0){
    if(source.empty()||depth>4)return false;
    const auto first=source.find_first_not_of(" \t"),last=source.find_last_not_of(" \t");
    if(first==std::string_view::npos)return false;
    if(first||last+1!=source.size()){
        std::string inner;if(!quantity(source.substr(first,last-first+1),language,inner,depth+1))return false;
        result=std::string(source.substr(0,first))+inner+std::string(source.substr(last+1));return true;
    }
    const auto scalar=[](std::string_view value){
        if(value=="—")return true;bool digit=false;
        for(unsigned char c:value){if(c>='0'&&c<='9'){digit=true;continue;}
            if(c!=' '&&c!='\t'&&c!='.'&&c!=','&&c!='-'&&c!='+')return false;
        }return digit;
    };
    for(const auto separator:{std::string_view(" / "),std::string_view(" – "),std::string_view(" … ")}){
        const auto at=source.find(separator);if(at==std::string_view::npos)continue;
        std::string left,right;
        if(quantity(source.substr(0,at),language,left,depth+1)&&quantity(source.substr(at+separator.size()),language,right,depth+1)){
            result=left+std::string(separator)+right;return true;
        }return false;
    }
    for(const auto unit:{std::string_view("Вт·ч"),std::string_view("МГц"),std::string_view("ГГц"),std::string_view("кГц"),std::string_view("Гц"),
        std::string_view("ГиБ"),std::string_view("МиБ"),std::string_view("ГБ"),std::string_view("МБ"),std::string_view("мА"),std::string_view("Вт"),std::string_view("В"),
        std::string_view("GHz"),std::string_view("MHz"),std::string_view("kHz"),std::string_view("Hz"),std::string_view("GiB"),std::string_view("MiB"),
        std::string_view("GB"),std::string_view("MB"),std::string_view("Wh"),std::string_view("mA"),std::string_view("W"),std::string_view("V"),std::string_view("°C"),std::string_view("%")}){
        if(source.size()<=unit.size()||!source.ends_with(unit))continue;
        const auto value=source.substr(0,source.size()-unit.size());if(!scalar(value))continue;
        const auto* e=exact(unit);result=std::string(value)+std::string(e?text(*e,language):unit);return true;
    }
    if(scalar(source)){result=std::string(source);return true;}return false;
}
inline bool match(std::string_view source,std::string_view pattern,std::vector<Argument>& args){
    std::size_t p=0,s=0;bool hasLiteral=false;
    while(p<pattern.size()){
        const auto open=pattern.find('{',p);
        const auto literal=pattern.substr(p,open==std::string_view::npos?pattern.size()-p:open-p);
        if(!source.substr(s).starts_with(literal))return false;
        hasLiteral|=!literal.empty();s+=literal.size();if(open==std::string_view::npos)return hasLiteral&&s==source.size();
        const auto close=pattern.find('}',open+1);if(close==std::string_view::npos||args.size()>=8)return false;
        const auto next=pattern.find('{',close+1);const auto suffix=pattern.substr(close+1,next==std::string_view::npos?pattern.size()-close-1:next-close-1);
        // Adjacent placeholders are supported by explicit formatMessage only:
        // inferring their boundary in an already rendered string is ambiguous.
        if(next!=std::string_view::npos&&suffix.empty())return false;
        std::size_t end=source.size();if(!suffix.empty()){end=source.find(suffix,s);if(end==std::string_view::npos)return false;}
        const auto name=pattern.substr(open+1,close-open-1),value=source.substr(s,end-s);
        if((name=="number"||name=="percent"||name=="seconds"||name=="count"||name=="level")&&!numeric(value))return false;
        args.emplace_back(name,value);s=end;p=close+1;
    }return hasLiteral&&s==source.size();
}
inline std::string render(std::string_view source,unsigned language,unsigned depth,bool& known){
    known=false;if(depth>8||source.size()>65536||source.empty())return std::string(source);
    // Battery header is a technical EC identity tuple, invariant in all locales.
    if(source.starts_with("Project ID ")&&source.find(" · Platform ID ")!=std::string_view::npos&&source.find(" · ROM ID ")!=std::string_view::npos){known=true;return std::string(source);}
    // These contextual OEM labels are deliberately invariant in RU/EN/SC/TC.
    // Retaining source keys lets both OS adapters refresh language repeatedly
    // without translating the mode Healthy as the inventory status Исправен.
    for(const auto name:{std::string_view("Full Capacity"),std::string_view("Balanced"),std::string_view("Healthy")})
        if(source==std::string("@battery-mode:")+std::string(name)){known=true;return std::string(name);}
    if(protectedName(source)||path(source)){known=true;return std::string(source);}
    if(const auto* entry=exact(source)){known=true;return std::string(text(*entry,language));}
    const auto first=source.find_first_not_of(" \t\r"),last=source.find_last_not_of(" \t\r");
    if(first!=std::string_view::npos&&(first!=0||last+1!=source.size())){
        bool innerKnown=false;auto inner=render(source.substr(first,last-first+1),language,depth+1,innerKnown);
        if(innerKnown){known=true;return std::string(source.substr(0,first))+inner+std::string(source.substr(last+1));}
    }
    if(source.ends_with(':'))if(const auto* e=exact(source.substr(0,source.size()-1))){known=true;return std::string(text(*e,language))+":";}
    for(const auto& e:translations())if(e.russian.find('{')!=std::string_view::npos){
        std::vector<Argument> args;
        if(match(source,e.russian,args)||((args.clear(),true)&&match(source,e.english,args))){
            // Reviewed detail/effect/mode/state fields are display messages. Quantities
            // pass the separate scalar/unit admission below. Other values,
            // owners, model names, paths and numeric payloads remain opaque.
            std::vector<std::string> storage;storage.reserve(args.size());
            for(auto& a:args){
                if(a.first=="detail"||a.first=="effect"||a.first=="mode"||a.first=="state"){
                    bool nested=false;storage.push_back(render(a.second,language,depth+1,nested));a.second=storage.back();
                }else if(a.first=="total"||a.first=="free"||a.first=="limit"||a.first=="enforced"||
                    a.first=="minimum"||a.first=="maximum"||a.first=="default"){
                    std::string value;if(quantity(a.second,language,value)){storage.push_back(std::move(value));a.second=storage.back();}
                }
            }
            known=true;return substitute(text(e,language),args);
        }
    }
    // Composite metric/status rows are admitted only when every piece is a
    // complete key, numeric field or opaque non-Cyrillic identifier. One unknown
    // Cyrillic segment keeps the whole row, rather than producing mixed words.
    for(const auto separator:{std::string_view(" · "),std::string_view("; "),std::string_view("\n")}){
        if(source.find(separator)==std::string_view::npos)continue;
        std::string output;bool complete=true,translated=false;std::size_t at=0;
        while(at<=source.size()){
            const auto end=source.find(separator,at);const auto part=source.substr(at,end==std::string_view::npos?source.size()-at:end-at);
            bool accepted=false;auto value=render(part,language,depth+1,accepted);
            if(!accepted){bool cyrillic=false;for(unsigned char c:part)if(c==0xD0||c==0xD1){cyrillic=true;break;}accepted=!cyrillic;}
            if(!accepted){complete=false;break;}translated|=value!=part;if(at)output+=separator;output+=value;
            if(end==std::string_view::npos)break;at=end+separator.size();
        }if(complete&&translated){known=true;return output;}
    }
    // Whole-label fields, not arbitrary sentence fragments. The label must be
    // an exact catalog key and the colon is an explicit display-field boundary.
    const auto colon=source.find(": ");if(colon!=std::string_view::npos){
        const auto label=source.substr(0,colon);if(const auto* e=exact(label)){
            bool valueKnown=false;const auto payload=render(source.substr(colon+2),language,depth+1,valueKnown);
            known=true;return std::string(text(*e,language))+": "+payload;
        }
    }
    // Numeric readouts keep number bytes; only complete unit/label tokens change.
    for(const auto& e:translations()){
        if(e.russian.find('{')!=std::string_view::npos)continue;
        if(source.starts_with(e.russian)&&source.size()>e.russian.size()&&source[e.russian.size()]==' '&&numeric(source.substr(e.russian.size()+1))){
            known=true;return std::string(text(e,language))+std::string(source.substr(e.russian.size()));
        }
    }
    std::string measured;if(quantity(source,language,measured)){known=true;return measured;}
    return std::string(source);
}
} // namespace detail
inline std::string translate(std::string_view source){
    bool known=false;return detail::render(source,languageIndex(),0,known);
}
// Arguments are opaque and substituted once; callers explicitly translate any
// semantic argument (for example a preview-channel label) before passing it.
inline std::string formatMessage(std::string_view source,
    std::initializer_list<std::pair<std::string_view,std::string_view>> arguments){
    const auto* entry=detail::exact(source);const auto pattern=entry?detail::text(*entry,languageIndex()):source;
    return detail::substitute(pattern,std::vector<detail::Argument>(arguments));
}
}
