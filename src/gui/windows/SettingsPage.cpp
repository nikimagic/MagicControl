#include "MagicControl/ControlAudit.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/DisplayContinuity.h"
#include "../common/KeyboardBrightness.h"
#include "../common/KeyboardIdleDisplay.h"
#include "SettingsCardsModel.h"
#include "LocalizationWindows.h"
#include "../../platform/windows/IntelTelemetryWindows.h"
#include "ControlTheme.h"
#include "PageMechanics.h"
#include "SettingsPage.h"
#include "../../platform/windows/SettingsOperationsWindows.h"
#include "../../platform/windows/SettingsActionWindows.h"
#include "../../platform/windows/LightingControlWindows.h"
#include "../../platform/windows/EcLoggerWindows.h"
#include "../../platform/windows/SettingsSnapshotWindows.h"
#include "../../platform/windows/BatterySettingsWindows.h"
#include "../../platform/windows/SettingsPersistenceWindows.h"
#include "../../platform/windows/SettingsPowerSelectionWindows.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "../../platform/windows/NativeBatteryWindows.h"
#include "../../platform/windows/IteLightingWindows.h"
#include "../common/BatteryPolicy.h"
#include <vector>
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/Logging.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/LaptopControl.h"
#include "MagicControl/KeyboardBacklight.h"

#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>

#include <array>
#include <algorithm>

#include <functional>
#include <format>
#include <cwchar>
#include <cmath>
#include <optional>
#include <string>

#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/FanCurve.h"
#include "MagicControl/ScrollModel.h"
#include "../../platform/windows/PowerDiagnosticsWindows.h"
#include "../../platform/windows/FanControlWindows.h"
#include "../../platform/windows/ApuPowerWindows.h"
#include "../../platform/windows/ApuPowerPolicyWindows.h"
#include "../../platform/windows/CustomModeWindows.h"
#include "../../platform/windows/BatteryChargeWindows.h"
#include "MagicControl/Performance.h"
#include "../../protocols/uniwill/x5sp4nag/PowerState.h"
#include "MagicControl/CustomPresets.h"
#include "../../devices/huawei/hlyl-wxx9/FanControlWindows.h"
#include "Layout.h"
#include "../common/Theme.h"
#include "OsdPopup.h"
#include "../../platform/windows/TextWindows.h"
#include "NumericFieldTheme.h"
#include "SelectableText.h"
#include "../common/NumericCommitDelay.h"
#include "../common/EcByteEditor.h"
#include "../../platform/windows/UniwillControlOwnerWindows.h"
#include "../common/OsdMessages.h"
#include "../common/MonitoringCards.h"
#include "MagicControl/BatteryResourceState.h"
#include "../common/KeyboardPulsePalette.h"
#include "../common/BatteryDetails.h"
#include "../common/CpuPowerLabels.h"
#include "../common/FooterStatus.h"
#include "../common/DeviceHeader.h"
#include "SettingsNotice.h"

#ifdef MAGICCONTROL_WINDOWS_SETTINGS_TEST
namespace mc::gui {
struct SettingsOperationsTestAccess {
    static void seed(SettingsOperationsWindows& model,const BatteryReading& sample){model.state.batteryTelemetry=sample;model.batteryResources.update(model.pageBoard,sample);}
};
}
#endif
namespace mc::gui {
namespace {
using namespace settings_controls;
constexpr UINT kCycleCommit=WM_APP+21;
constexpr UINT_PTR kRestartApplyTimer=75;
constexpr UINT_PTR kRefreshTimer = 74;
// Previews use RGB rather than reverse-scaled EC levels. Raw 255/255/255
// extinguished the keyboard on this board; white uses equal EC levels 50/50/50.
constexpr std::array<std::array<std::uint8_t, 3>, 8> kColorPreviews{{
    {255, 255, 255}, {255, 0, 0}, {255, 80, 0}, {255, 204, 0},
    {0, 255, 0}, {0, 255, 255}, {0, 0, 255}, {180, 0, 255}}};

void fill(HDC dc, RECT rect, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void paintTimeoutCheckbox(HWND window, HDC dc) {
    RECT rect{}; GetClientRect(window,&rect);
    const int dpi = static_cast<int>(GetDpiForWindow(window));
    const int edge = std::max(1,MulDiv(kKeyboardTimeoutCheckBorder,dpi,96));
    const bool enabled = IsWindowEnabled(window) != FALSE;
    const bool checked = SendMessageW(window,BM_GETCHECK,0,0) == BST_CHECKED;
    const bool focused = enabled && GetFocus() == window;
    const bool pressed = enabled && (SendMessageW(window,BM_GETSTATE,0,0) & BST_PUSHED) != 0;
    fill(dc,rect,pressed ? theme::kAccentPressed : enabled ? theme::kSurfaceRaised : theme::kNumericDisabledBackground);
    const COLORREF border = focused ? theme::kAccentHover : checked && enabled ? theme::kAccent : theme::kNumericBorder;
    fill(dc,{rect.left,rect.top,rect.right,rect.top+edge},border);
    fill(dc,{rect.left,rect.bottom-edge,rect.right,rect.bottom},border);
    fill(dc,{rect.left,rect.top,rect.left+edge,rect.bottom},border);
    fill(dc,{rect.right-edge,rect.top,rect.right,rect.bottom},border);
    if (checked) {
        const int width = rect.right - rect.left, height = rect.bottom - rect.top;
        const POINT tick[3]{{rect.left+width*24/100,rect.top+height*50/100},
                            {rect.left+width*43/100,rect.top+height*70/100},
                            {rect.left+width*77/100,rect.top+height*29/100}};
        const HPEN pen = CreatePen(PS_SOLID,std::max(1,MulDiv(2,dpi,96)),enabled ? theme::kAccentHover : theme::kTextMuted);
        const HGDIOBJ previous = SelectObject(dc,pen);
        Polyline(dc,tick,3); SelectObject(dc,previous); DeleteObject(pen);
    }
}

LRESULT CALLBACK timeoutCheckboxProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR id, DWORD_PTR) {
    // Keep BUTTON's native checkbox input, state and accessibility behaviour.
    // Only its system white glyph is replaced with the application palette.
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window,&paint);
        paintTimeoutCheckbox(window,dc); EndPaint(window,&paint); return 0;
    }
    case WM_PRINTCLIENT: paintTimeoutCheckbox(window,reinterpret_cast<HDC>(wParam)); return 0;
    case WM_ENABLE: case WM_SETFOCUS: case WM_KILLFOCUS:
    case BM_SETCHECK: case BM_SETSTATE: case WM_THEMECHANGED:
    case WM_DPICHANGED_AFTERPARENT: {
        const auto result = DefSubclassProc(window,message,wParam,lParam);
        InvalidateRect(window,nullptr,FALSE); return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(window,timeoutCheckboxProcedure,id);
        break;
    }
    return DefSubclassProc(window,message,wParam,lParam);
}

void drawLabel(HDC dc, const wchar_t* value, RECT rect, HFONT font, COLORREF color,
               UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    const HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    ::mc::gui::localizedDrawText(dc, value, -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, previous);
}

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (count > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

}

struct SettingsPage::Impl : SettingsOperationsWindows::View {
    SettingsOperationsWindows model;
    decltype(SettingsOperationsWindows::Presentation::closing) closing{model.presentation().closing};
    decltype(SettingsOperationsWindows::Presentation::loggerObservation) loggerObservation{model.presentation().loggerObservation};
    decltype(SettingsOperationsWindows::Presentation::cycleSettings) cycleSettings{model.presentation().cycleSettings};
    decltype(SettingsOperationsWindows::Presentation::selectedLightingZone) selectedLightingZone{model.presentation().selectedLightingZone};
    decltype(SettingsOperationsWindows::Presentation::batteryCardView) batteryCardView{model.presentation().batteryCardView};
    decltype(SettingsOperationsWindows::Presentation::pageBoard) pageBoard{model.presentation().pageBoard};
    decltype(SettingsOperationsWindows::Presentation::preview) preview{model.presentation().preview};
    decltype(SettingsOperationsWindows::Presentation::gm5Ui) gm5Ui{model.presentation().gm5Ui};
    decltype(SettingsOperationsWindows::Presentation::gm6NativePower) gm6NativePower{model.presentation().gm6NativePower};
    decltype(SettingsOperationsWindows::Presentation::intelCpuUi) intelCpuUi{model.presentation().intelCpuUi};
    decltype(SettingsOperationsWindows::Presentation::huaweiUi) huaweiUi{model.presentation().huaweiUi};
    decltype(SettingsOperationsWindows::Presentation::selectedCustomPreset) selectedCustomPreset{model.presentation().selectedCustomPreset};
    decltype(SettingsOperationsWindows::Presentation::powerValues) powerValues{model.presentation().powerValues};
    decltype(SettingsOperationsWindows::Presentation::powerOptionValues) powerOptionValues{model.presentation().powerOptionValues};
    decltype(SettingsOperationsWindows::Presentation::powerEditing) powerEditing{model.presentation().powerEditing};
    decltype(SettingsOperationsWindows::Presentation::busy) busy{model.presentation().busy};
    decltype(SettingsOperationsWindows::Presentation::reading) reading{model.presentation().reading};
    decltype(SettingsOperationsWindows::Presentation::loaded) loaded{model.presentation().loaded};
    decltype(SettingsOperationsWindows::Presentation::state) state{model.presentation().state};
    decltype(SettingsOperationsWindows::Presentation::batteryResources) batteryResources{model.presentation().batteryResources};
    decltype(SettingsOperationsWindows::Presentation::ec) ec{model.presentation().ec};
    decltype(SettingsOperationsWindows::Presentation::oemControl) oemControl{model.presentation().oemControl};
    decltype(SettingsOperationsWindows::Presentation::gm6Controller) gm6Controller{model.presentation().gm6Controller};
    decltype(SettingsOperationsWindows::Presentation::nativeBattery) nativeBattery{model.presentation().nativeBattery};
    decltype(SettingsOperationsWindows::Presentation::voltageBattery) voltageBattery{model.presentation().voltageBattery};
    decltype(SettingsOperationsWindows::Presentation::iteLighting) iteLighting{model.presentation().iteLighting};
    decltype(SettingsOperationsWindows::Presentation::controller) controller{model.presentation().controller};
    decltype(SettingsOperationsWindows::Presentation::cycle) cycle{model.presentation().cycle};
    bool synchronizingPower{},powerFieldsShowManual{},lastFanDefaultLock{};int powerOptionRowsLaidOut{};
    int requestedCommand{};osd::Message requestedNotice;

    page::Buffer buffer;


    HWND viewport{}, scrollbar{}, page{}, customColorButton{};
    ScrollModel scroll;
    bool scrollHot{}, scrollDragging{};
    int scrollGrabOffset{};
    std::array<HWND, 4> effectButtons{};
    HWND cycleEdit{},cycleSpin{};RECT cycleCaption{},cycleUnit{};
    HWND restartEdit{},restartSpin{};RECT restartCaption{},restartUnit{};
    ui::NumericCommitDelay restartDelay;audit::Id restartAuditId{};
    NumericFieldWindows loggerAddress{},loggerValue{};
    HWND loggerBytes{};
    HWND lightingZoneTitle{};
    std::array<HWND,3> voltageButtons{};
    std::array<HWND,2> agingButtons{};
    std::array<HWND, ui::batteryThresholdCount> batteryButtons{};
    std::array<HWND, 5> performanceButtons{};
    std::array<HWND, 8> colorButtons{};
    std::array<HWND,5> levelButtons{};
    std::array<RECT, 3> cardBounds{}, cardHeadings{}, cardDetails{};
    RECT batteryPortDetail{}, batteryFlowDetail{}, batteryInfoDetail{};
    ui::SetsGeometry setsLayout;
    NumericFieldWindows keyboardIdleReadout;
    ui::KeyboardIdleGeometry keyboardIdleGeometry;
    std::array<RECT, 3> apuDiagnostics{};
    RECT colorCaption{};
    HWND nativeButton{};
    HWND manualPowerButton{};
    std::array<HWND,3> customPresetButtons{};
    std::array<HWND,3> apuPolicySwitch{};
    std::array<HWND,2> customBaseButtons{};RECT customBaseCaption{};
    std::array<HWND,2> usbPriorityButtons{};
    RECT apuPolicyCaption{};
    RECT manualPowerHint{};
    std::array<HWND, 3> powerEdits{}, powerSpins{};
    std::array<NumericFieldWindows,6> powerOptions{};
    std::array<RECT, 3> powerCaptions{};
    // Core/storage order stays STAPM/Slow/Fast; visual/Tab order is Fast/Slow/STAPM.
    HWND keyboardTimeoutCheck{}, keyboardTimeoutEdit{}, keyboardTimeoutSpin{};
    std::vector<HWND> hiddenByResource;
    std::array<bool,3> visibleCards{true,true,true};
    RECT keyboardTimeoutCaption{}, keyboardTimeoutUnit{};
    HFONT body{}, bold{};
    HBRUSH background{CreateSolidBrush(theme::kBackground)};
    HBRUSH numericBackground{CreateSolidBrush(theme::kSurfaceRaised)};
    std::array<COLORREF, 16> customColors{};
    OsdPopup* popup{};
    page::RegionTooltips batteryTooltips;
    UINT dpi{96};unsigned fontLanguage{~0u};
    int width{}, height{};
    bool visible{},pulsationPalette{},cycleUi{};
    std::uint64_t paintCount{};

    explicit Impl(HWND parent, EcAccess* access, OsdPopup* popupWindow, std::function<void(std::string)> logger,
                  std::shared_ptr<windows::FanController> control,SettingsDispatch::ThreadFactory factory)
        : model(*this,access,std::move(logger),std::move(control),ui::presentationBoard(),ui::laptopPreview(),std::move(factory)),popup(popupWindow) {
        WNDCLASSW klass{};
        klass.lpfnWndProc = procedure;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"MagicControl.SettingsPage";
        klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        RegisterClassW(&klass);
        WNDCLASSW viewportClass = klass;
        viewportClass.lpfnWndProc = viewportProcedure;
        viewportClass.lpszClassName = L"MagicControl.SettingsViewport";
        RegisterClassW(&viewportClass);
        WNDCLASSW scrollbarClass = klass;
        scrollbarClass.lpfnWndProc = scrollbarProcedure;
        scrollbarClass.lpszClassName = L"MagicControl.SettingsScrollbar";
        RegisterClassW(&scrollbarClass);
        viewport = CreateWindowExW(WS_EX_CONTROLPARENT, viewportClass.lpszClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN | WS_TABSTOP, 0, 0, 0, 0, parent, nullptr, klass.hInstance, this);
        page = CreateWindowExW(WS_EX_CONTROLPARENT, klass.lpszClassName, L"SETs",
                               WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, viewport, nullptr,
                               klass.hInstance, this);

        scrollbar = CreateWindowExW(0, scrollbarClass.lpszClassName, L"",
            WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, viewport, nullptr, klass.hInstance, this);
        model.setStatus(L"Читаю текущие состояния…");
        const auto& batteryNames=ui::batteryPolicyNames();
        const auto performanceNames=ui::cpuPowerLabels(deviceProfile(pageBoard).id);
        const wchar_t* effectNames[] = {L"Моно", L"Пульсация", L"Мульти",L"Цикл 6"};
        for (int i = 0; i < ui::batteryThresholdCount; ++i) batteryButtons[i] = makeButton(kBatteryFirst + i, wide(batteryNames[i]).c_str());
        agingButtons[0]=makeButton(kBatteryAgingOff,L"Выкл.");agingButtons[1]=makeButton(kBatteryAgingOn,L"Вкл.");
        for(int i=0;i<3;++i)voltageButtons[i]=makeButton(kBatteryVoltageFirst+i,wide(ui::batteryModeNames(deviceProfile(pageBoard).batteryModeLabels)[i]).c_str());
        for (int i = 0; i < 5; ++i) performanceButtons[i] = makeButton(kPerformanceFirst + i, wide(performanceNames[i]).c_str());
        ShowWindow(performanceButtons[0],SW_HIDE);
        ShowWindow(performanceButtons[4],SW_HIDE);
        for (int i = 0; i < 4; ++i) effectButtons[i] = makeButton(120 + i, effectNames[i]);
        const wchar_t* colors[] = {L"Белый", L"Красный", L"Оранжевый", L"Жёлтый", L"Зелёный", L"Голубой", L"Синий", L"Фиолетовый"};
        for (int i = 0; i < 8; ++i) colorButtons[i] = makeButton(130 + i, colors[i]);
        for(int level=0;level<5;++level)levelButtons[level]=makeButton(kLevelFirst+level,wide(ui::keyboardBrightnessButtonCaption(level)).c_str());
        customColorButton = makeButton(kCustomColor, L"Свой цвет…");
        nativeButton = makeButton(kNativeMode,wide(ui::keyboardBrightnessDefaultCaption).c_str());
        lightingZoneTitle=makeButton(kLightingZoneTitle,L"");ShowWindow(lightingZoneTitle,SW_HIDE);
        if(iteLighting)SetWindowTextW(effectButtons[3],L"Эффекты…");
        createKeyboardTimeoutField();
        keyboardIdleReadout=createNumericFieldWindows(page,kKeyboardIdleValue,kKeyboardIdleSpin,0,600,0,16);
        SendMessageW(keyboardIdleReadout.edit,EM_SETREADONLY,TRUE,0);
        SendMessageW(keyboardIdleReadout.spin,UDM_SETBUDDY,0,0);
        ShowWindow(keyboardIdleReadout.spin,SW_HIDE);
        const auto restart=createNumericFieldWindows(page,kRestartEdit,kRestartSpin,1,99,20,2);
        restartEdit=restart.edit;restartSpin=restart.spin;
        if(ui::showEcLoggerCard){
            loggerAddress=createNumericFieldWindows(page,kLoggerAddress,kLoggerAddressSpin,0,ecEditorMaximumAddress,0x770,6,true);
            loggerValue=createNumericFieldWindows(page,kLoggerValue,kLoggerValueSpin,0,255,0,4,true);
            loggerBytes=createSelectableText(page);setSelectableText(loggerBytes,L"Введите адрес и нажмите Enter");
        }
        const auto interval=createNumericFieldWindows(page,kCycleEdit,kCycleSpin,1,3600,cycleSettings.intervalSeconds);
        cycleEdit=interval.edit;cycleSpin=interval.spin;
        createPowerFields();
        if(customBaseSupported(deviceProfile(pageBoard).id)){
            customBaseButtons[0]=makeButton(kCustomBaseZero,L"00");
            customBaseButtons[1]=makeButton(kCustomBaseTurbo,L"10");
        }
        model.initializePowerDraft();
        if(gm6Controller)for(int i=0;i<3;++i)SendMessageW(powerSpins[i],UDM_SETRANGE32,0,windows::gmCustomFieldMaximum(deviceProfile(pageBoard).id,i));
        if(huaweiUi||ui::showEcPolicyControls){
            apuPolicySwitch[0] = makeButton(kApuPolicyOff, huaweiUi?L"Выкл.":wide(ui::ecPolicyButtonNames[0]).c_str());
            apuPolicySwitch[1] = makeButton(kApuPolicyOn, huaweiUi?L"Вкл.":wide(ui::ecPolicyButtonNames[1]).c_str());
            if(!huaweiUi)apuPolicySwitch[2] = makeButton(kApuPolicyOriginal,L"7/FF");
        }
        if(ui::showUsbPriorityControls){
            usbPriorityButtons[0]=makeButton(kUsbPriorityBattery,L"Battery");
            usbPriorityButtons[1]=makeButton(kUsbPriorityPerformance,L"Performance");
        }
        createFonts();
        model.bindReady([window=page]{PostMessageW(window,WM_APP+10,0,0);});
    }

    ~Impl() {
        model.beginClosing();if(page)KillTimer(page,kRestartApplyTimer);
        if (page) KillTimer(page, kRefreshTimer);
        model.shutdown();
        if (viewport) DestroyWindow(viewport);
        if (body) DeleteObject(body);
        if (bold) DeleteObject(bold);
        DeleteObject(background);
        DeleteObject(numericBackground);
    }

    void createKeyboardTimeoutField() {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_UPDOWN_CLASS};
        InitCommonControlsEx(&controls);
        keyboardTimeoutCheck = CreateWindowExW(0,L"BUTTON",L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,0,0,0,0,page,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kKeyboardTimeoutCheck)),GetModuleHandleW(nullptr),nullptr);
        const auto timeout=createNumericFieldWindows(page,kKeyboardTimeoutEdit,kKeyboardTimeoutSpin,0,3600,0);
        keyboardTimeoutEdit=timeout.edit;keyboardTimeoutSpin=timeout.spin;
        if(!keyboardTimeoutCheck||!SetWindowSubclass(keyboardTimeoutCheck,timeoutCheckboxProcedure,kKeyboardTimeoutCheck,0))
            throw std::runtime_error("Не удалось создать поле таймера подсветки.");
        SendMessageW(keyboardTimeoutEdit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"—"));
        SetWindowTextW(keyboardTimeoutEdit,L"");
        SendMessageW(keyboardTimeoutCheck,BM_SETCHECK,BST_UNCHECKED,0);
        EnableWindow(keyboardTimeoutCheck,FALSE);
        EnableWindow(keyboardTimeoutEdit,FALSE);
        EnableWindow(keyboardTimeoutSpin,FALSE);
        // Removed from SETs for every model. Preserve backend/settings only.
        for(auto h:{keyboardTimeoutCheck,keyboardTimeoutEdit,keyboardTimeoutSpin})ShowWindow(h,SW_HIDE);
    }




    std::size_t powerOptionCount()const{return std::min<std::size_t>(6,std::max(deviceProfile(pageBoard).powerOptions.size(),state.oem.customPowerOptions.size()));}
    std::wstring powerOptionLabel(std::size_t i)const{
        const auto declared=deviceProfile(pageBoard).powerOptions;
        std::string label,unit;
        if(i<declared.size()){label=declared[i].label;unit=declared[i].unit;}
        else if(i<state.oem.customPowerOptions.size()){label=state.oem.customPowerOptions[i].label;unit=state.oem.customPowerOptions[i].unit;}else return {};
        return wide(ui::cpuPowerOptionLabel(deviceProfile(pageBoard).id,label,unit));
    }
    const PowerSetting* powerOption(std::size_t i)const{
        const auto declared=deviceProfile(pageBoard).powerOptions;
        if(i<declared.size()){const auto found=std::find_if(state.oem.customPowerOptions.begin(),state.oem.customPowerOptions.end(),[&](const auto& p){return p.key==declared[i].key;});
            return found==state.oem.customPowerOptions.end()?nullptr:&*found;}
        return i<state.oem.customPowerOptions.size()?&state.oem.customPowerOptions[i]:nullptr;
    }
    bool extendedPower()const{return powerOptionCount()!=0;}
    int powerOptionRows()const{return static_cast<int>((powerOptionCount()+2)/3);}
    void createPowerFields() {
        manualPowerButton = makeButton(kManualPower, L"Custom");
        for(int i=0;i<3;++i){customPresetButtons[i]=makeButton(kCustomPresetFirst+i,std::to_wstring(i+1).c_str());EnableWindow(customPresetButtons[i],FALSE);}
        model.initializePowerEditors();
        for (int i = 2; i >= 0; --i) {
            const auto field=createNumericFieldWindows(page,kPowerEditFirst+i,kPowerSpinFirst+i,
                5,kApuManualMaximum,powerValues[i],3);
            powerEdits[i]=field.edit;powerSpins[i]=field.spin;
        }
        for(int i=0;i<6;++i){powerOptions[i]=createNumericFieldWindows(page,kPowerOptionEditFirst+i,kPowerOptionSpinFirst+i,0,100000,0,6);
            for(auto h:{powerOptions[i].edit,powerOptions[i].spin}){ShowWindow(h,SW_HIDE);EnableWindow(h,FALSE);}}
        syncPowerFields(!preview&&!gm6Controller);
    }
    void syncPowerFields(bool manualValues) {
        synchronizingPower = true;
        const auto actual=observedSettingsPowerLimits(state,bool(gm6Controller),model.wujiePower());
        for (int i = 0; i < 3; ++i) {
            if (manualValues) SendMessageW(powerSpins[i], UDM_SETPOS32, 0, powerValues[i]);
            else {
                // Read-only fields show observed limits (Wujie: accepted EC),
                // never the manual draft/factory table. Do not clamp telemetry.
                const auto text = gm6Controller&&actual[i]==0?std::wstring(L"Auto"):
                    actual[i] ? (deviceProfile(pageBoard).id=="minisforum-hpbsd"?wide(ui::displayNumber(*actual[i],1)):std::format(L"{:.0f}", *actual[i])) : L"—";
                SetWindowTextW(powerEdits[i], text.c_str());
            }
        }
        syncPowerOptions(manualValues);
        synchronizingPower = false;
    }




    bool readPowerFields(std::array<int, 3>& values) {
        for(int i=0;i<3;++i){wchar_t text[16]{};GetWindowTextW(powerEdits[i],text,16);
            const auto value=windows::parseCustomField(text,model.customMinimum(i),model.customMaximum(i));
            if(!value){model.setStatus(windows::customFieldsError(deviceProfile(pageBoard).id,bool(gm6Controller),bool(oemControl)));return false;}values[i]=*value;}
        return true;
    }
    bool readPowerOptions(PowerOptions& values)override{
        if(!extendedPower())return true;
        for(std::size_t i=0;i<powerOptionCount();++i){const auto field=powerOption(i);if(!field)return false;wchar_t text[32]{};GetWindowTextW(powerOptions[i].edit,text,32);
            const auto value=windows::parseCustomField(text,field->minimum,field->maximum);
            if(!value||!field->accepts(*value)){model.setStatus(L"Custom: значение отсутствует в допустимом диапазоне или списке WMI.");return false;}values[field->key]=*value;}
        return true;
    }
    void syncPowerOptions(bool editing){
        for(int i=0;i<6;++i){const auto field=powerOption(i);if(!field){SetWindowTextW(powerOptions[i].edit,L"—");continue;}
            const auto value=editing&&powerOptionValues.contains(field->key)?std::optional<int>(powerOptionValues.at(field->key)):field->actual;
            SetWindowTextW(powerOptions[i].edit,value?std::to_wstring(*value).c_str():L"—");}
    }
    void commitPowerOption(int index){
        if(synchronizingPower||!powerFieldsShowManual)return;
        const auto field=powerOption(index);if(!field)return;wchar_t text[32]{};GetWindowTextW(powerOptions[index].edit,text,32);
        if(const auto value=windows::parseCustomField(text,field->minimum,field->maximum))model.editPowerOptionDraft(field->key,*value);
        if(powerOptionValues.contains(field->key))SetWindowTextW(powerOptions[index].edit,std::to_wstring(powerOptionValues.at(field->key)).c_str());
    }
    void stepPowerOption(int index,int direction){
        commitPowerOption(index);const auto field=powerOption(index);if(!field||!field->writable||!powerOptionValues.contains(field->key))return;
        const int current=powerOptionValues.at(field->key);int next=current;
        if(field->discrete.empty())next=std::clamp(current+(direction>0?field->step:-field->step),field->minimum,field->maximum);
        else if(direction>0){const auto it=std::upper_bound(field->discrete.begin(),field->discrete.end(),current);if(it!=field->discrete.end())next=*it;}
        else{const auto it=std::lower_bound(field->discrete.begin(),field->discrete.end(),current);if(it!=field->discrete.begin())next=*std::prev(it);}
        model.editPowerOptionDraft(field->key,next);SetWindowTextW(powerOptions[index].edit,std::to_wstring(next).c_str());
    }
    void commitPowerField(int index) {
        if (synchronizingPower || !powerFieldsShowManual) return;
        wchar_t text[16]{}; GetWindowTextW(powerEdits[index], text, 16);
        model.editPowerDraftField(index,text);
        // Clamp on completion only: users can erase and retype intermediate values.
        synchronizingPower = true;
        SendMessageW(powerSpins[index], UDM_SETPOS32, 0, powerValues[index]);
        synchronizingPower = false;
        // Editing only changes the in-memory draft. No timer, EC write or save.
    }


    int powerButtonCount()const{return ui::cpuPowerButtonCount(deviceProfile(pageBoard).id);}
    osd::Message notice(int id)const{
        return settingsNotice({id,deviceProfile(pageBoard).id,bool(oemControl),bool(gm6Controller),state.onBattery,
            model.batteryAutoActive(),bool(iteLighting),selectedLightingZone,model.brightnessMaximum(),model.effectIndex(),model.pulsePalette(),deviceProfile(pageBoard).batteryModeLabels});
    }
    osd::Message notice(const windows::SettingsAction& action)const override{
        if(action.kind==windows::SettingsActionKind::BatteryVoltage)return notice(kBatteryVoltageFirst+action.value);
        if(action.kind==windows::SettingsActionKind::CustomEditor)return notice(kManualPower);
        return notice(0);
    }
    void statusChanged(const std::wstring& value) override {ui::footerStatuses().publish("sets",mc::windows::encoding::utf8(value));}
    std::uint64_t beginNotice(const osd::Message& value) override {return popup?popup->beginOperation(value):0;}
    void completeNotice(std::uint64_t token,osd::Message value) override {if(popup&&token)popup->completeOperation(token,std::move(value));}
    void loggerChanged() override {
            if(!loggerBytes)return;
            if(loggerObservation){SendMessageW(loggerValue.spin,UDM_SETPOS32,0,loggerObservation->value);
                SetWindowTextW(loggerValue.edit,std::format(L"0x{:02X}",loggerObservation->value).c_str());}
            else SetWindowTextW(loggerValue.edit,L"—");
            if(loggerObservation){const auto rendered=ui::ecFieldText(*loggerObservation);
                setSelectableText(loggerBytes,wide(rendered.text),static_cast<LONG>(rendered.activeStart),static_cast<LONG>(rendered.activeLength));}
            else setSelectableText(loggerBytes,L"Нет подтверждённого снимка. Введите адрес и нажмите Enter.");    }
    HWND makeStatic(const wchar_t* text) {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
                               0, 0, 0, 0, page, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    HWND makeButton(int id, const wchar_t* text) {
        HWND button = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, page,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (button) control::themeButton(button);
        return button;
    }

    int scale(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    int scrollLane() const { return scale(12); }
    void scrollTo(int position) {
        const int previous = scroll.position();
        scroll.setPosition(position);
        if (scroll.position() != previous)
            SetWindowPos(page, nullptr, 0, -scroll.position(), 0, 0,
                SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
        InvalidateRect(viewport, nullptr, FALSE);
        InvalidateRect(scrollbar, nullptr, FALSE);
    }
    void wheel(int delta) {
        page::wheel(scroll,delta,dpi,[this](int position){scrollTo(position);});
    }
    RECT thumb() const {
        return page::scrollThumb(width, height, scroll, dpi, scrollHot, scrollDragging);
    }
    void drawThumb(HDC dc) const {
        page::drawThumb(dc, thumb(), scrollHot, scrollDragging);
    }
    void revealFocus() {
        const HWND focus = GetFocus();
        if (!focus || !IsChild(page, focus)) return;
        RECT rect{}; GetWindowRect(focus, &rect);
        MapWindowPoints(nullptr, viewport, reinterpret_cast<POINT*>(&rect), 2);
        if (rect.top < scale(4)) scrollTo(scroll.position() + rect.top - scale(4));
        else if (rect.bottom > height - scale(4))
            scrollTo(scroll.position() + rect.bottom - height + scale(4));
    }

    void createFonts() {
        fontLanguage=ui::languageIndex();
        if (body) DeleteObject(body);
        if (bold) DeleteObject(bold);
        body = createUiFont(ui::FontRole::ClassicBody, dpi);
        bold = createUiFont(ui::FontRole::ClassicHeading, dpi);


    }

    void place(HWND control, int x, int y, int w, int h) {
        page::place(control, x, y, w, h);
        if (reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)) != body)
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(body), FALSE);
    }

    // КОНТРАКТ КАРТОЧЕК И ПРОКРУТКИ SETs (согласовано 16.10.2026).
    // Геометрия карточек получает ПОЛНУЮ ширину вкладки: не вычитать scrollLane().
    // viewport неподвижен, page с содержимым перемещается только по Y; scrollbar —
    // отдельный непрозрачный сосед page, HWND_TOP, в правой полосе шириной 12 DIP.
    // Полоса не входит в движущееся содержимое и не сужает карточки. Рисовать thumb
    // только в scrollbarProcedure, не дублировать в viewport/page. При прокрутке,
    // hover/drag и изменении размеров инвалидировать сам scrollbar.
    // extent — нижняя граница ВСЕГО содержимого; viewport — видимая высота.
    // Скролл скрывается только когда содержимое помещается, без таймера автоскрытия.
    // Высота карточки обязана включать последнюю строку и нижний отступ. После
    // удаления строки сдвигать последующие строки И уменьшать высоту/extent.
    // Сохранить согласованный ряд APU: мощность слева, Fast/Slow/STAPM + Manual
    // компактной группой справа. Не возвращать удалённые подписи и пустые строки.
    // Правки SETs не должны менять геометрию других вкладок или обращаться к EC
    // из layout/paint/scroll. Сборка сама по себе не подтверждает внешний вид.
    std::array<std::wstring,3> powerFieldLabels() const {
        const auto names=ui::cpuPowerFieldLabels(deviceProfile(pageBoard).id,state.gm6.pl4Double);
        return {wide(names[0]),wide(names[1]),wide(names[2])};
    }
    void arrange() {
        for(auto h:hiddenByResource)ShowWindow(h,SW_SHOWNA);
        hiddenByResource.clear();
        if (!visible || width < scale(200) || height < scale(100)) return;
        HDC dc = GetDC(page);
        const HGDIOBJ previous = SelectObject(dc, body);
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        SIZE cycleLabelSize{},cycleUnitSize{},cycleValueSize{},defaultSize{},offSize{},digitSize{};
        ::mc::gui::localizedTextExtent(dc,L"Default",7,&defaultSize);
        ::mc::gui::localizedTextExtent(dc,L"Выкл.",5,&offSize);GetTextExtentPoint32W(dc,L"4",1,&digitSize);
        ::mc::gui::localizedTextExtent(dc,L"Каждые",6,&cycleLabelSize);
        ::mc::gui::localizedTextExtent(dc,L"сек",3,&cycleUnitSize);
        GetTextExtentPoint32W(dc,L"3600",4,&cycleValueSize);
        const std::array<int,3> cycleWidths{static_cast<int>(cycleLabelSize.cx),static_cast<int>(cycleUnitSize.cx),static_cast<int>(cycleValueSize.cx)};
        SelectObject(dc, previous);
        ReleaseDC(page, dc);
        // The content is a separate child: scrolling never moves the viewport or its thumb.
        const int contentWidth = std::max(1, width);
        const auto cards=windowsSettingsCards(deviceProfile(pageBoard),state.osBattery,state.ecBattery,state.batteryPolicy,batteryLoggingEnabled(),state.voltageBattery.targetVoltageMv);
        visibleCards=cards.visible;
        auto g = ui::setsGeometry(contentWidth, {static_cast<int>(dpi)},
                                       {static_cast<int>(metrics.tmHeight), static_cast<int>(metrics.tmAscent)}, 3, true, true, cards.showDiagnostics,cycleWidths,{static_cast<int>(defaultSize.cx),static_cast<int>(offSize.cx),static_cast<int>(digitSize.cx)},model.brightnessMaximum(),
                                       {static_cast<int>(cards.diagnostics[0].size()),static_cast<int>(cards.diagnostics[1].size())},stockCpuProfileCount(deviceProfile(pageBoard).id),ui::ecPolicyButtonCount(deviceProfile(pageBoard).id),cards.visible,powerOptionRows());
        // Equal visible gaps require measured captions, not equal caption boxes:
        // Пик/Средняя/Длительно have different glyph widths. Text touches its own editor;
        // Labels include exactly one trailing space; groups and Manual have 8 DIP gaps.
        const auto powerLabels = powerFieldLabels();
        std::array<int, 3> captionWidths{};
        HDC captionDc = GetDC(page);
        const auto oldCaptionFont = SelectObject(captionDc, body);
        const auto textWidth=[&](const wchar_t* text){SIZE size{};::mc::gui::localizedTextExtent(captionDc,text,static_cast<int>(wcslen(text)),&size);return static_cast<int>(size.cx);};
        if(!huaweiUi)ui::placeEcPolicyCaption(g,{static_cast<int>(dpi)},textWidth(wide(ui::ecPolicyCaption(255,255)).c_str()));
        if(ui::showUsbPriorityControls&&model.wujiePower()){
            SIZE battery{},performance{};
            GetTextExtentPoint32W(captionDc,L"Battery",7,&battery);
            GetTextExtentPoint32W(captionDc,L"Performance",11,&performance);
            ui::placeUsbPowerPriority(g,{static_cast<int>(dpi)},battery.cx,performance.cx);
        }
        ui::placeBatteryIdentity(g,{static_cast<int>(dpi)},textWidth(L"Циклов: 999 · Здоровье: 100,0%"));
        ui::placeCombinedBatteryControls(g,{static_cast<int>(dpi)},textWidth(wide(ui::batteryIdentityCaption(255,255,255)).c_str()));
        ui::placeLoggerFields(g,{static_cast<int>(dpi)},{textWidth(L"EC["),textWidth(L"] == ["),textWidth(L"]")});
        keyboardIdleGeometry=ui::placeKeyboardIdle(g.headings[2],{static_cast<int>(dpi)},
            textWidth(L"Выключение подсветки через"),textWidth(L"Отключено"),textWidth(L"сек"));
        for (int i = 0; i < 3; ++i) {
            SIZE textSize{};
            const auto caption = powerLabels[intelCpuUi?i:2-i];
            ::mc::gui::localizedTextExtent(captionDc, caption.c_str(), static_cast<int>(caption.size()), &textSize);
            captionWidths[i] = textSize.cx;
        }
        SIZE hintSize{};
        constexpr wchar_t hintText[] = L"Отредактируйте и нажмите Custom";
        ::mc::gui::localizedTextExtent(captionDc, hintText, static_cast<int>(wcslen(hintText)), &hintSize);
        manualPowerHint = {g.manualPowerButton.right - hintSize.cx,
            g.manualPowerButton.bottom + scale(6), g.manualPowerButton.right,
            g.manualPowerButton.bottom + scale(6) + std::max(hintSize.cy, metrics.tmHeight)};
        const int powerDigitsWidth=textWidth(L"250");
        const int combinedAgingCaptionWidth=textWidth(wide(ui::translate(ui::combinedBatteryAgingCaption)).c_str());
        const int agingButtonTextWidth=std::max(textWidth(L"Выкл."),textWidth(L"Вкл."));
        const int batteryIdentityWidth=textWidth(wide(ui::batteryIdentityCaption(255,255,255)).c_str());
        if(extendedPower()){
            std::array<int,6> widths{};
            for(int i=0;i<6;++i){const auto label=powerOptionLabel(i);widths[i]=textWidth(label.c_str());}
            ui::placePowerFieldsWithOptions(g,{static_cast<int>(dpi)},captionWidths,widths,powerDigitsWidth,powerOptionRows());
        }else ui::placePowerFields(g,{static_cast<int>(dpi)},captionWidths,powerDigitsWidth);
        SelectObject(captionDc, oldCaptionFont); ReleaseDC(page, captionDc);
        setsLayout=g;
        for(int i=0;i<6;++i){const bool shown=i<static_cast<int>(powerOptionCount())&&visibleCards[1];
            for(auto h:{powerOptions[i].edit,powerOptions[i].spin})ShowWindow(h,shown?SW_SHOW:SW_HIDE);
            if(shown){auto bounds=g.powerOptionEdits[i];bounds.right=g.powerOptionSpins[i].right;placeNumericFieldWindows(powerOptions[i],bounds,g.powerOptionSpins[i].width(),0,body);}}
        placeNumericFieldWindows(keyboardIdleReadout,keyboardIdleGeometry.value,0,0,body);
        ShowWindow(keyboardIdleReadout.spin,SW_HIDE);
        scroll.setViewport(height);
        scroll.setExtent(g.extent);
        SetWindowPos(scrollbar, HWND_TOP, width - scrollLane(), 0, scrollLane(), height,
            SWP_NOACTIVATE | (scroll.scrollable() ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        page::place(page, 0, -scroll.position(), contentWidth, std::max(height, g.extent));
        for (std::size_t row = 0; row < apuDiagnostics.size(); ++row)
            apuDiagnostics[row] = toNativeRect(g.apuDiagnostics[row]);
        const auto put = [this](HWND control, ui::Rect r) {
            place(control, r.left, r.top, r.width(), r.height());
        };
        restartCaption=toNativeRect(g.restartCaption);restartUnit=toNativeRect(g.restartUnit);
        auto restartBounds=g.restartEdit;restartBounds.right=g.restartSpin.right;
        placeNumericFieldWindows({restartEdit,restartSpin},restartBounds,g.restartSpin.width(),0,body);
        const bool loggerVisible=cards.showDiagnostics;
        for(auto h:{loggerAddress.edit,loggerAddress.spin,loggerValue.edit,loggerValue.spin,loggerBytes})ShowWindow(h,loggerVisible?SW_SHOW:SW_HIDE);
        if(loggerVisible){auto address=g.loggerAddressEdit;address.right=g.loggerAddressSpin.right;
            auto value=g.loggerValueEdit;value.right=g.loggerValueSpin.right;
            placeNumericFieldWindows(loggerAddress,address,g.loggerAddressSpin.width(),0,body);
            placeNumericFieldWindows(loggerValue,value,g.loggerValueSpin.width(),0,body);
            put(loggerBytes,g.loggerMulti);SendMessageW(loggerBytes,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);}
        for (int i = 0; i < 3; ++i) {
            const int powerColumn = intelCpuUi?i:2-i;
            powerCaptions[i] = toNativeRect(g.powerCaptions[powerColumn]);
            auto fieldBounds=g.powerEdits[powerColumn];fieldBounds.right=g.powerSpins[powerColumn].right;
            placeNumericFieldWindows({powerEdits[i],powerSpins[i]},fieldBounds,g.powerSpins[powerColumn].width(),0,body);
            cardBounds[i] = toNativeRect(g.cards[i]);
            cardHeadings[i] = toNativeRect(g.headings[i]);
            cardDetails[i] = toNativeRect(g.details[i]);
            if (i == 0) {
                batteryPortDetail = toNativeRect(g.portDetail);
                batteryInfoDetail = batteryPortDetail;
                batteryInfoDetail.left = std::max(batteryInfoDetail.left, batteryInfoDetail.right - scale(250));
                batteryFlowDetail = batteryPortDetail;
                batteryFlowDetail.right = batteryInfoDetail.left - scale(12);
                batteryFlowDetail.left = std::max(batteryFlowDetail.left, batteryFlowDetail.right - scale(160));
                batteryPortDetail.right = std::max(batteryPortDetail.left, batteryFlowDetail.left - scale(12));
            }
            put(effectButtons[i], g.effects[i]);
        }
        for (int i = 0; i < ui::batteryThresholdCount; ++i) put(batteryButtons[i], g.battery[i]);
        ui::placeCombinedBatteryStatus(g,{static_cast<int>(dpi)},combinedAgingCaptionWidth,batteryIdentityWidth,agingButtonTextWidth);
        put(agingButtons[0],g.agingOff);put(agingButtons[1],g.agingOn);setsLayout=g;
        for(int i=0;i<3;++i)put(voltageButtons[i],g.batteryVoltage[i]);
        put(effectButtons[3],g.effects[3]);
        const int stockCount=stockCpuProfileCount(deviceProfile(pageBoard).id);
        for(int i=0;i<stockCount;++i)put(performanceButtons[i+1],g.performance[i]);
        for(int i=0;i<5;++i)ShowWindow(performanceButtons[i],i>=1&&i<=stockCount?SW_SHOW:SW_HIDE);
        put(manualPowerButton, g.manualPowerButton);
        for(int i=0;i<3;++i)put(customPresetButtons[i],g.customPowerPresets[i]);
        if(customBaseSupported(deviceProfile(pageBoard).id)){
            const int right=g.cards[1].right-scale(12),top=g.cards[1].top+scale(4),width=scale(50),height=scale(26);
            put(customBaseButtons[0],{right-2*width,top,right-width,top+height});
            put(customBaseButtons[1],{right-width,top,right,top+height});
            customBaseCaption={right-2*width-scale(115),top,right-2*width-scale(8),top+height};
            for(auto h:customBaseButtons)ShowWindow(h,visibleCards[1]?SW_SHOWNA:SW_HIDE);
        }
        put(apuPolicySwitch[0], g.apuPolicyOff);
        put(apuPolicySwitch[1], g.apuPolicyOn);
        const bool originalPolicy=nativeEcPolicyProfile(deviceProfile(pageBoard).id);
        if(originalPolicy)put(apuPolicySwitch[2],g.apuPolicyOriginal);
        ShowWindow(apuPolicySwitch[2],ui::showEcPolicyControls&&originalPolicy?SW_SHOW:SW_HIDE);
        for(std::size_t i=0;i<usbPriorityButtons.size();++i){
            if(model.wujiePower())put(usbPriorityButtons[i],g.usbPowerPriority[i]);
            ShowWindow(usbPriorityButtons[i],ui::showUsbPriorityControls&&model.wujiePower()?SW_SHOWNA:SW_HIDE);
        }
        apuPolicyCaption = toNativeRect(g.apuPolicyCaption);
        pulsationPalette = !iteLighting&&model.effectIndex() == 1;
        int colorSlot = 0;
        // Порядок UI фиксирован. Физические цвета другой прошивки исправляются
        // только таблицей model.pulsePalette().presets, не перестановкой этих кнопок.
        for (int i = 0; i < 8; ++i) {
            const bool shown = model.effectIndex()==3?i<6:!pulsationPalette || model.pulsePalette().presets[i] != 0;
            const wchar_t* names[]{L"Белый",L"Красный",L"Оранжевый",L"Жёлтый",L"Зелёный",L"Голубой",L"Синий",L"Фиолетовый"};
            const auto label=model.effectIndex()==3?L"Цвет "+std::to_wstring(i+1):pulsationPalette&&model.pulsePalette().numbered?std::to_wstring(model.pulsePalette().presets[i]):std::wstring(names[i]);
            SetWindowTextW(colorButtons[i],label.c_str());
            if (shown) put(colorButtons[i], g.colors[colorSlot++]);
            ShowWindow(colorButtons[i], shown ? SW_SHOWNA : SW_HIDE);
        }
        for(int level=0;level<5;++level){const bool shown=level<=model.brightnessMaximum();
            if(shown)put(levelButtons[level],g.brightnessLevels[level]);ShowWindow(levelButtons[level],shown?SW_SHOWNA:SW_HIDE);}
        colorCaption = toNativeRect(g.colorCaption);
        auto customColorBounds = g.customColor;
        if (pulsationPalette) {
            customColorBounds.right = g.colors[colorSlot].left + customColorBounds.width();
            customColorBounds.left = g.colors[colorSlot].left;
        }
        put(customColorButton, customColorBounds);
        const bool cycling=model.effectIndex()==3;cycleUi=cycling;
        ShowWindow(customColorButton,ui::customKeyboardColorVisible(model.effectIndex())?SW_SHOWNA:SW_HIDE);
        auto cycleBounds=g.cycleEdit;cycleBounds.right=g.cycleSpin.right;
        placeNumericFieldWindows({cycleEdit,cycleSpin},cycleBounds,g.cycleSpin.width(),0,body);
        ShowWindow(cycleEdit,cycling?SW_SHOWNA:SW_HIDE);ShowWindow(cycleSpin,cycling?SW_SHOWNA:SW_HIDE);
        cycleCaption=toNativeRect(g.cycleCaption);cycleUnit=toNativeRect(g.cycleUnit);
        put(nativeButton, g.nativeButton);
        put(lightingZoneTitle,g.headings[2]);
        const auto title=wide(ui::translate(lightingZoneTitleText())+" ▾");SetWindowTextW(lightingZoneTitle,title.c_str());
        ShowWindow(lightingZoneTitle,visibleCards[2]&&state.lighting.zones.size()>1?SW_SHOWNA:SW_HIDE);
        put(keyboardTimeoutCheck,g.keyboardTimeoutCheck);
        auto timeoutBounds=g.keyboardTimeoutEdit;timeoutBounds.right=g.keyboardTimeoutSpin.right;
        placeNumericFieldWindows({keyboardTimeoutEdit,keyboardTimeoutSpin},timeoutBounds,g.keyboardTimeoutSpin.width(),0,body);
        keyboardTimeoutCaption=toNativeRect(g.keyboardTimeoutCaption);
        keyboardTimeoutUnit=toNativeRect(g.keyboardTimeoutUnit);
        for(auto h:{keyboardTimeoutCheck,keyboardTimeoutEdit,keyboardTimeoutSpin}){ShowWindow(h,SW_HIDE);EnableWindow(h,FALSE);}

        const auto hide=[&](HWND h){if(h&&(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)){hiddenByResource.push_back(h);ShowWindow(h,SW_HIDE);}};
        if(!visibleCards[0]){
            for(auto h:{restartEdit,restartSpin})hide(h);
            for(const auto& list:{std::vector<HWND>(batteryButtons.begin(),batteryButtons.end()),std::vector<HWND>(voltageButtons.begin(),voltageButtons.end()),std::vector<HWND>(agingButtons.begin(),agingButtons.end())})for(auto h:list)hide(h);
        }
        if(!visibleCards[2]){
            for(auto h:{lightingZoneTitle,customColorButton,nativeButton,cycleEdit,cycleSpin,keyboardIdleReadout.edit,keyboardIdleReadout.spin})hide(h);
            for(const auto& list:{std::vector<HWND>(effectButtons.begin(),effectButtons.end()),std::vector<HWND>(colorButtons.begin(),colorButtons.end()),std::vector<HWND>(levelButtons.begin(),levelButtons.end())})for(auto h:list)hide(h);
        }
        InvalidateRect(page, nullptr, FALSE);
        InvalidateRect(viewport, nullptr, FALSE);
        InvalidateRect(scrollbar, nullptr, FALSE);
    }




    const char* lightingZoneTitleText()const{return mc::lightingZoneTitle(selectedLightingZone);}
    void syncLightingTitle(){
        const auto title=wide(ui::translate(lightingZoneTitleText())+" ▾");SetWindowTextW(lightingZoneTitle,title.c_str());
        ShowWindow(lightingZoneTitle,visibleCards[2]&&state.lighting.zones.size()>1?SW_SHOWNA:SW_HIDE);
        EnableWindow(lightingZoneTitle,!busy&&!deviceOperations().interactiveBusy());
        InvalidateRect(page,&cardHeadings[2],FALSE);
    }


    void chooseLightingZone(){
        if(busy||deviceOperations().interactiveBusy()||state.lighting.zones.size()<2)return;
        HMENU menu=CreatePopupMenu();
        for(std::size_t i=0;i<state.lighting.zones.size();++i){const auto& info=state.lighting.zones[i];
            const auto name=wide(ui::translate(mc::lightingZoneTitle(info.zone)));
            AppendMenuW(menu,MF_STRING|(info.zone==selectedLightingZone?MF_CHECKED:0),static_cast<UINT_PTR>(i+1),name.c_str());}
        RECT bounds{};GetWindowRect(lightingZoneTitle,&bounds);
        const auto selected=control::trackPopup(menu,TPM_LEFTALIGN|TPM_TOPALIGN,bounds.left,bounds.bottom,page);DestroyMenu(menu);
        if(!selected||selected>state.lighting.zones.size())return;
        // Viewing another zone is entirely local: no read/apply/save command.
        model.selectLightingZoneLocal(state.lighting.zones[selected-1].zone);
        renderSelection();updateButtons();arrange();
    }
    void syncBatteryCard(){
        const auto& aging=state.ecBattery.aging;
        for(int i=0;i<2;++i){
            EnableWindow(agingButtons[i],preview||((!busy||reading)&&aging.writable&&
                !state.ecBattery.retainedForDisplay&&!state.ecBattery.battery.dynamicStale&&(!i||aging.error.empty())));
            setSelected(agingButtons[i],preview?model.previewState().batteryAging==i:aging.enabled&&*aging.enabled==bool(i));
        }
        const bool capacity=batteryCardView==ui::BatteryCardView::Capacity;
        const bool oemModes=state.oem.chargeModeWritable||state.oem.chargeMode.has_value();
        ShowWindow(restartEdit,SW_HIDE);ShowWindow(restartSpin,SW_HIDE);
        restartDelay.cancel();KillTimer(page,kRestartApplyTimer);
        for(auto b:batteryButtons)ShowWindow(b,visibleCards[0]?SW_SHOWNA:SW_HIDE);
        const auto& n=state.voltageBattery;
        const bool nativeAvailable=n.writable||n.voltageTransitionWritable;
        for(int i=0;i<3;++i){ShowWindow(voltageButtons[i],visibleCards[0]?SW_SHOWNA:SW_HIDE);
            // Background reads may queue one interactive command through start;
            // active writes still block buttons and never run concurrently.
            EnableWindow(voltageButtons[i],preview|| (oemModes?(!preview&&(!busy||reading)&&!deviceOperations().interactiveBusy()&&state.oem.chargeModeWritable):windows::nativeVoltageButtonEnabled(
                {preview,busy,reading,bool(voltageBattery),nativeAvailable,nativeAvailable,n.appPresent},i,
                []{return deviceOperations().interactiveBusy();})));
            setSelected(voltageButtons[i],preview?model.previewState().batteryMode==i:oemModes?state.oem.chargeMode==i:!capacity&&n.mode&&static_cast<int>(*n.mode)==i);}
        // The power card keeps its original policy-control positions.
        if(!oemControl)for(int i=0;i<ui::ecPolicyButtonCount(deviceProfile(pageBoard).id);++i)ShowWindow(apuPolicySwitch[i],ui::showEcPolicyControls&&capacity?SW_SHOWNA:SW_HIDE);
    }
    void chooseHidEffect(){
        if(!iteLighting||busy)return;auto c=model.hidConfiguration();HMENU menu=CreatePopupMenu();
        const std::array<const char*,4> names{"Моно","Пульсация","Волна","Мульти"};constexpr std::array ids{1,2,3,5};
        for(int i=0;i<4;++i)AppendMenuW(menu,MF_STRING|(c.effect==ids[i]?MF_CHECKED:0),ids[i],wide(ui::translate(names[i])).c_str());
        HMENU speed=CreatePopupMenu(),direction=CreatePopupMenu();
        for(int i=0;i<5;++i)AppendMenuW(speed,MF_STRING|(c.speed==i?MF_CHECKED:0),100+i,std::to_wstring(i+1).c_str());
        AppendMenuW(direction,MF_STRING|(c.direction==0?MF_CHECKED:0),110,wide(ui::translate("Влево")).c_str());
        AppendMenuW(direction,MF_STRING|(c.direction==1?MF_CHECKED:0),111,wide(ui::translate("Вправо")).c_str());
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(speed),wide(ui::translate("Скорость")).c_str());
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(direction),wide(ui::translate("Направление")).c_str());
        RECT bounds{};GetWindowRect(effectButtons[3],&bounds);const auto selected=control::trackPopup(menu,TPM_LEFTALIGN|TPM_TOPALIGN,bounds.left,bounds.bottom,page);DestroyMenu(menu);
        const auto choice=selectHidLightingMenu(c,static_cast<int>(selected));if(!choice)return;
        requestedCommand=kEffectFirst;
        requestedNotice=model.lightingNotice(choice->description);
        model.submit(requestedCommand,requestedNotice,windows::SettingsAction::zoneRequest(choice->configuration),L"Применяю эффект подсветки…");
    }


    const auto& colorValues()const{return keyboardColorPalette(model.lightingModel());}






    void syncBrightness(){select(levelButtons,model.currentBrightness().value_or(-1));}

    int paletteIndex() const {
        return observedLightingPalette(state,bool(iteLighting),selectedLightingZone,model.lightingModel(),deviceProfile(pageBoard).id,kColorPreviews);
    }
    std::wstring batterySummary() const {
        if(nativeBattery)return wide(state.nativeBattery.status);
        if (!state.valid) return L"Читаю состояние EC…";
        const int index = model.batteryIndex();
        if (index < 0 || index >= ui::batteryThresholdCount) return L"EC не сообщил выбранный предел заряда.";
        const std::wstring suffix = index == 0 ? L" · без ограничения" : state.batteryPlatform==7 ? L" · платформа 7 · исполнение не проверено" :
            (state.batteryGate & 4) ? L" · ограничение включено EC" : L" · включение не подтверждено EC";
        return L"Выбранный верхний предел заряда: " + wide(ui::batteryThresholdNames[index]) + suffix;
    }
    std::wstring batteryPortSummary() const {
        if(const auto adapter=ui::lenovoAdapterPowerSummary(deviceProfile(pageBoard).id,preview,state.batteryTelemetry,state.ecBattery))return wide(*adapter);
        return windows::batteryPortSummary(state,preview,bool(nativeBattery),bool(gm6Controller),bool(oemControl),bool(ec),
            [this]{const auto view=batteryResources.view();
                return wide(ui::batteryPowerStatus(state.batteryTelemetry,&view));});
    }
    static std::wstring watts(std::optional<int> value) { return (value ? std::to_wstring(*value) : L"—") + L" Вт"; }
    static std::wstring measuredWatts(std::optional<double> value) {
        if (!value) return L"— Вт";
        auto text = wide(ui::displayNumber(*value,1));
        return text + L" Вт";
    }
    static COLORREF apuPowerColor(std::optional<double> watts) {
        if (!watts) return theme::kTextMuted;
        // Fractional telemetry uses continuous bands: <=20, <=30, <=45, >45 W.
        if (*watts <= 20) return RGB(90, 165, 255);
        if (*watts <= 30) return theme::kBatteryCharging;
        if (*watts <= 45) return theme::kWarning;
        return theme::kBatteryDischarging;
    }
    COLORREF displayColor(int index) const {
        if(model.effectIndex()==3&&index<6){const auto& c=cycleSettings.colors[index];return RGB(c[0],c[1],c[2]);}
        if (!iteLighting&&model.effectIndex() == 1) {
            if (index == 0) return RGB(255, 255, 255);
            if (index == 4) return RGB(0, 128, 0);
        }
        const auto& value = kColorPreviews[static_cast<std::size_t>(index)];
        return rgb(value[0], value[1], value[2]);
    }
    void paintBatteryReading(HDC dc,const BatteryReading& reading,const ui::BatteryRowsGeometry<6>& rows,bool lenovoEc=false){
        const auto metrics=ui::batteryCurrentMetrics(reading,lenovoEc);
        constexpr UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
        for(std::size_t i=0;i<metrics.size();++i){
            const auto color=ui::batteryCurrentColor(reading,i);
            drawLabel(dc,wide(metrics[i].label).c_str(),toNativeRect(rows.labels[i]),body,i==1?color:theme::kTextSecondary,flags);
            drawLabel(dc,wide(metrics[i].value).c_str(),toNativeRect(rows.values[i]),body,color,flags);
        }

    }

    void paint() {
        ++paintCount;
        page::Paint painting(page, buffer);
        if (!painting.valid()) return;
        const HDC dc = painting.dc();
        const auto& client = painting.client();
        fill(dc, client, theme::kBackground);
        const auto batteryTitle=wide(ui::translate("Батарея"));
        const auto lightingTitle=wide(ui::translate(lightingZoneTitleText()));
        const wchar_t* headings[] = {
            batteryTitle.c_str(),
            intelCpuUi?L"Производительность Intel CPU":L"Производительность APU",
            lightingTitle.c_str()};
        for (std::size_t i = 0; i < cardBounds.size(); ++i) {
            if(!visibleCards[i])continue;
            fill(dc, cardBounds[i], theme::kSurface);
            drawLabel(dc, headings[i], cardHeadings[i], bold, theme::kTextPrimary);
        }
        if(visibleCards[0]){
        const bool ecIdentityAvailable=state.valid&&ec&&!preview&&!oemControl;
        const auto headerPlatform=nativeBattery?state.nativeBattery.platformRaw:ecIdentityAvailable?std::optional<std::uint8_t>{state.batteryPlatform}:std::nullopt;
        const auto headerRom=nativeBattery?state.nativeBattery.romIdRaw:ecIdentityAvailable?std::optional<std::uint8_t>{state.batteryRom}:std::nullopt;
        const auto headerProject=nativeBattery?state.nativeBattery.projectRaw:ecIdentityAvailable?std::optional<std::uint8_t>{state.batteryProject}:std::nullopt;
        if(ui::showBatteryEcIdentity(deviceProfile(pageBoard)))drawLabel(dc,wide(ui::batteryIdentityCaption(headerProject,headerPlatform,headerRom)).c_str(),toNativeRect(setsLayout.batteryDeviceIdentity),body,theme::kTextSecondary,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        const auto measure=[&](const std::string& value){const auto text=wide(value);SIZE size{};const auto old=SelectObject(dc,body);::mc::gui::localizedTextExtent(dc,text.c_str(),static_cast<int>(text.size()),&size);SelectObject(dc,old);return size.cx;};
        const auto cards=windowsSettingsCards(deviceProfile(pageBoard),state.osBattery,state.ecBattery,state.batteryPolicy,batteryLoggingEnabled(),state.voltageBattery.targetVoltageMv);
        const auto& times=cards.primary;const auto& sources=cards.sources;
        for(std::size_t card=0;card<sources.size();++card){
            fill(dc,toNativeRect(setsLayout.batteryInfoCards[card]),theme::kFanNestedSurface);
            drawLabel(dc,wide(sources[card]).c_str(),toNativeRect(setsLayout.batteryInfoHeadings[card]),bold,theme::kTextPrimary);
        }
        std::array<int,ui::primaryBatteryRows> widths{};
        for(std::size_t row=0;row<times.size();++row)widths[row]=measure(times[row].value);
        std::array<std::array<int,6>,2> currentWidths{};
        const auto& readings=cards.current;
        const bool lenovoEc=deviceProfile(pageBoard).id=="lenovo-legion-kwcn";
        for(std::size_t column=0;column<2;++column){const auto metrics=ui::batteryCurrentMetrics(readings[column],column==0&&lenovoEc);
            for(std::size_t row=0;row<metrics.size();++row)currentWidths[column][row]=measure(metrics[row].value);}
        const auto primaryRows=ui::batteryPrimaryRowsGeometry(setsLayout,{static_cast<int>(dpi)},currentWidths,widths);
        paintBatteryReading(dc,readings[0],primaryRows.current[0],lenovoEc);
        paintBatteryReading(dc,readings[1],primaryRows.current[1]);
        const auto& batteryRows=primaryRows.details;
        auto tooltips=ui::batteryPrimaryTooltips(primaryRows,deviceProfile(pageBoard).id=="lenovo-legion-kwcn");
        if(ui::showUsbPriorityControls&&model.wujiePower()&&state.usbPriorityStatus&&!state.usbPriorityStatus->writable)
            for(const auto& bounds:setsLayout.usbPowerPriority)tooltips.push_back({bounds,state.usbPriorityStatus->reason});
        batteryTooltips.update(page,tooltips,body);
        constexpr UINT infoFlags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
        for(std::size_t row=0;row<times.size();++row){
            const auto& source=sources[row/((ui::primaryBatteryRows+1)/2)];
            const auto label=ui::translate(times[row].label)+(times[row].source==source?"":" · "+times[row].source);
            drawLabel(dc,wide(label).c_str(),toNativeRect(batteryRows.labels[row]),body,theme::kTextSecondary,infoFlags);
            drawLabel(dc,wide(times[row].value).c_str(),toNativeRect(batteryRows.values[row]),body,ui::primaryBatteryValueColor(row,times[row].value),infoFlags);
        }
        if(cards.showDiagnostics){
            fill(dc,toNativeRect(setsLayout.batteryComparison),theme::kSurface);
            drawLabel(dc,L"Логгер",toNativeRect(setsLayout.loggerHeading),bold,theme::kTextPrimary);
            drawLabel(dc,L"EC[",toNativeRect(setsLayout.loggerPrefix),body,theme::kTextSecondary);
            drawLabel(dc,L"] == [",toNativeRect(setsLayout.loggerMiddle),body,theme::kTextSecondary);
            drawLabel(dc,L"]",toNativeRect(setsLayout.loggerSuffix),body,theme::kTextSecondary);
            const auto formats=ui::ecEditorRangeCaption();
            drawLabel(dc,wide(formats).c_str(),toNativeRect(setsLayout.loggerFormats),body,theme::kTextPrimary,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            const auto meaning=ui::ecRegisterDescription(deviceProfile(pageBoard).id,loggerObservation?loggerObservation->address:0x770);
            drawLabel(dc,wide(meaning).c_str(),toNativeRect(setsLayout.loggerMeaning),body,theme::kTextSecondary,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
            const auto known=ui::ecKnownValues(deviceProfile(pageBoard).id,loggerObservation?loggerObservation->address:0x770);
            drawLabel(dc,wide(known).c_str(),toNativeRect(setsLayout.loggerKnown),body,theme::kTextSecondary,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
            const auto& columns=cards.diagnostics;
            constexpr UINT detailFlags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
            for(std::size_t column=0;column<columns.size();++column){
                const auto& block=setsLayout.batteryDetailColumns[column];
                fill(dc,toNativeRect(block.card),theme::kFanNestedSurface);
                const auto title=sources[column]+" · "+ui::translate("Подробные сведения");
                drawLabel(dc,wide(title).c_str(),toNativeRect(block.heading),bold,theme::kTextPrimary);
                const auto detailStatus=column==0?ui::ecBatteryStatus(state.ecBattery):state.osBattery.source;
                drawLabel(dc,wide(detailStatus).c_str(),toNativeRect(block.status),body,theme::kTextSecondary,detailFlags);
                std::array<int,ui::ecBatteryDetailRows> valueWidths{};
                for(std::size_t row=0;row<columns[column].size()&&row<valueWidths.size();++row)valueWidths[row]=measure(columns[column][row].value);
                const auto detailRows=ui::batteryDetailRowsGeometry(setsLayout,{static_cast<int>(dpi)},static_cast<int>(column),valueWidths);
                for(std::size_t row=0;row<columns[column].size()&&row<detailRows.labels.size();++row){
                    drawLabel(dc,wide(columns[column][row].label).c_str(),toNativeRect(detailRows.labels[row]),body,theme::kTextSecondary,detailFlags);
                    drawLabel(dc,wide(columns[column][row].value).c_str(),toNativeRect(detailRows.values[row]),body,theme::kTextPrimary,detailFlags);
                }
                if(column==0)drawLabel(dc,L"Raw: назначение не подтверждено; пороги не доказывают остановку зарядки.",toNativeRect(block.note),body,theme::kTextMuted,DT_LEFT|DT_WORDBREAK);
            }
        }
        if(customBaseSupported(deviceProfile(pageBoard).id))drawLabel(dc,L"Custom Base",customBaseCaption,body,theme::kTextSecondary,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        const auto& v=state.voltageBattery;
        const auto portText = batteryPortSummary();
        const auto voltageText = wide((v.voltageRetained?ui::translate("Сохранённые данные · "):std::string{})+
            ui::batteryVoltageTargets(v.baseVoltageMv,v.targetVoltageMv));
        const auto batteryView=batteryResources.view();
        const auto batteryCard = ui::batteryCard(state.batteryTelemetry,&batteryView);
        const auto flowText = wide(batteryCard.left[1].label + " " + batteryCard.left[1].value);
        // Fit the port, charge power and voltage summary in the shared row.
        const auto batteryFont = SelectObject(dc, body);
        SIZE portSize{}, flowSize{};
        ::mc::gui::localizedTextExtent(dc, portText.c_str(), static_cast<int>(portText.size()), &portSize);
        ::mc::gui::localizedTextExtent(dc, flowText.c_str(), static_cast<int>(flowText.size()), &flowSize);
        SelectObject(dc, batteryFont);
        auto portRect = toNativeRect(setsLayout.portDetail), flowRect = portRect;
        const auto identityRight=ui::showBatteryEcIdentity(deviceProfile(pageBoard))?setsLayout.batteryDeviceIdentity.left:setsLayout.portDetail.right;
        portRect.right=std::max<LONG>(portRect.left,identityRight-scale(12));
        const bool showFlow = batteryCard.flow == BatteryFlow::Charging;
        const int gap = scale(8);
        const int rowRight=portRect.right;
        const int rowWidth=std::max(0,rowRight-static_cast<int>(portRect.left));
        const int flowReserve=showFlow?std::min<int>(flowSize.cx+gap,rowWidth/3):0;
        portRect.right=std::max<LONG>(portRect.left,std::min<LONG>(portRect.left+portSize.cx,rowRight-flowReserve));
        drawLabel(dc,wide(ui::translate(ui::combinedBatteryAgingCaption)).c_str(),toNativeRect(setsLayout.agingCaption),body,theme::kTextSecondary,
            DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        flowRect.left = std::min<LONG>(rowRight,portRect.right + gap);
        flowRect.right=std::min<LONG>(rowRight,flowRect.left+(showFlow?flowSize.cx:0));
        drawLabel(dc, portText.c_str(), portRect, body, theme::kTextSecondary,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const auto flowColor = batteryCard.flow == BatteryFlow::Charging ? theme::kBatteryCharging :
            batteryCard.flow == BatteryFlow::Discharging ? theme::kBatteryDischarging : theme::kTextSecondary;
        // SETs hides discharge power; FANs retains its full battery telemetry.
        if (showFlow)
            drawLabel(dc, flowText.c_str(), flowRect, body, flowColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE|DT_END_ELLIPSIS);
        if(!voltageText.empty()){
            const RECT voltageRect=toNativeRect(setsLayout.batteryVoltageSummary);
            drawLabel(dc,voltageText.c_str(),voltageRect,body,theme::kTextSecondary,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        const auto healthValue=resourceDisplayValue(&batteryView,ResourceKind::Battery,0,"health");
        const auto cyclesValue=resourceDisplayUnsigned(&batteryView,ResourceKind::Battery,0,"cycles");
        std::wstring health = healthValue ? wide(ui::displayNumber(*healthValue,1))+L"%" : L"—";
        const auto info = L"Циклов: " + (cyclesValue ? std::to_wstring(*cyclesValue) : L"—") +
            L" · Здоровье: " + health;
        drawLabel(dc, info.c_str(), toNativeRect(setsLayout.batteryIdentity), body, theme::kTextSecondary,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        const auto& apu = state.apu;
        const std::array<std::wstring, 3> apuText{
            L"Мощность APU (SMU Fast): " + measuredWatts(apu.fastPowerWatts),
            L"Лимиты SMU:",
            apu.status.empty() ? L"Читаю фактические лимиты APU…" : wide(apu.status) +
                (apu.stale && apu.fastPowerWatts ? L" · последнее полученное значение" :
                 apu.fastPowerWatts ? L" · обновление каждые 3 секунды" : L"")
        };
        const auto actualWatts=intelCpuUi?state.intelWatts:apu.fastPowerWatts;
        const auto powerValue=measuredWatts(actualWatts);
        std::wstring powerCaption = intelCpuUi?L"Мощность CPU (Package): ":L"Мощность APU (SMU Fast): ";
        const auto previousFont = SelectObject(dc, body);
        SIZE captionSize{},valueSize{};
        ::mc::gui::localizedTextExtent(dc, powerCaption.c_str(), static_cast<int>(powerCaption.size()), &captionSize);
        ::mc::gui::localizedTextExtent(dc, powerValue.c_str(), static_cast<int>(powerValue.size()), &valueSize);
        if(extendedPower()&&captionSize.cx+valueSize.cx>apuDiagnostics[0].right-apuDiagnostics[0].left){
            powerCaption=intelCpuUi?L"Package: ":L"APU: ";
            ::mc::gui::localizedTextExtent(dc,powerCaption.c_str(),static_cast<int>(powerCaption.size()),&captionSize);
            if(captionSize.cx+valueSize.cx>apuDiagnostics[0].right-apuDiagnostics[0].left){powerCaption.clear();captionSize.cx=0;}
        }
        SelectObject(dc, previousFont);
        RECT powerLabel = apuDiagnostics[0], powerNumber = apuDiagnostics[0];
        powerLabel.right = std::min(powerLabel.right, powerLabel.left + captionSize.cx);
        powerNumber.left = powerLabel.right;
        drawLabel(dc, powerCaption.c_str(), powerLabel, body, theme::kTextPrimary);
        drawLabel(dc, powerValue.c_str(), powerNumber, body, apuPowerColor(actualWatts));
        for (std::size_t row = 2; row < apuText.size(); ++row) {
            RECT textRect = apuDiagnostics[row];
            if (powerEditing) textRect.right = std::min(textRect.right, manualPowerHint.left - scale(12));
            const auto intelDiagnostic=[&]{
                std::wstring text=gm5Ui?L"AMD SPL/SPPT/FPPT · EC-запросы · исполнение SMU не измерено":L"PL1/PL2/PL4 · EC-запросы · CPU Package: "+
                    wide(state.intelPowerSource.empty()?state.intelPowerStatus.empty()?"ожидаю две выборки Intel RAPL":state.intelPowerStatus:state.intelPowerSource);
                if(!state.gm6.cpuPresetWritable)text+=L" · "+wide(state.gm6.cpuPresetStatus);
                return text;
            };
            const auto diagnostic=preview?L"Просмотр профиля · аппаратное управление отключено":extendedPower()?wide(state.oem.status):gm6Controller?intelDiagnostic():intelCpuUi?L"Intel: управление PL для этой модели не подключено":model.wujiePower()?wide(wujiePowerComparisonText(state)):apuText[row];
            drawLabel(dc, diagnostic.c_str(), textRect, body,
                      row == 2 ? theme::kTextMuted : theme::kTextPrimary,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        // Editing-only guidance; no extra row or card resize in passive display.
        if (powerEditing) drawLabel(dc, L"Отредактируйте и нажмите Custom", manualPowerHint,
            body, theme::kBatteryCharging, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        const auto labels = powerFieldLabels();
        for (int i = 0; i < 3; ++i) drawLabel(dc, labels[i].c_str(), powerCaptions[i], body, theme::kTextSecondary,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS); // No gap before the editor; spacing follows it.
        if(extendedPower()){
            drawLabel(dc,L"GPU",toNativeRect(setsLayout.powerOptionHeaders[0]),body,theme::kTextSecondary);
            for(std::size_t i=0;i<powerOptionCount();++i)drawLabel(dc,powerOptionLabel(i).c_str(),toNativeRect(setsLayout.powerOptionCaptions[i]),body,theme::kTextSecondary,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        if(visibleCards[2]){
        drawLabel(dc, model.effectIndex()==3?L"Цвета":L"Цвет", colorCaption, body, theme::kTextSecondary);
        if(model.effectIndex()==3){drawLabel(dc,L"Каждые",cycleCaption,body,theme::kTextSecondary);drawLabel(dc,L"сек",cycleUnit,body,theme::kTextSecondary);}
        drawLabel(dc,L"Выключение подсветки через",toNativeRect(keyboardIdleGeometry.caption),body,theme::kTextSecondary,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        drawLabel(dc,L"сек",toNativeRect(keyboardIdleGeometry.unit),body,theme::kTextSecondary);
        }
        painting.finish();

    }







    void refreshKeyboardIdleReadout(){
        const auto value=wide(ui::keyboardIdleValue(state.valid&&!preview?state.keyboardIdle:std::nullopt));
        wchar_t current[32]{};GetWindowTextW(keyboardIdleReadout.edit,current,32);
        if(value!=current)SetWindowTextW(keyboardIdleReadout.edit,value.c_str());
    }




    void updateLightingButtons(bool ready,bool hidPalette,bool nativeBranch) {
        const auto availability=lightingButtonAvailability({ready,state.keyboardSupported,state.keyboardWritable,
            hidPalette,model.effectIndex(),model.brightnessMaximum(),model.pulsePalette().presets});
        const auto set=[nativeBranch](HWND button,bool value,bool always=false){if(nativeBranch||always||(IsWindowEnabled(button)!=FALSE)!=value)EnableWindow(button,value);};
        for(auto b:effectButtons)set(b,availability.effect);
        for(std::size_t i=0;i<levelButtons.size();++i)set(levelButtons[i],availability.levels[i],true);
        for(std::size_t i=0;i<colorButtons.size();++i)set(colorButtons[i],availability.colors[i]);
        set(customColorButton,availability.custom);set(nativeButton,availability.native);
        set(cycleEdit,availability.cycle,true);set(cycleSpin,availability.cycle,true);
    }
    void updateButtons() {
        for(auto h:customBaseButtons)if(h)EnableWindow(h,model.customBaseAvailable());
        const BOOL usbPriorityReady=model.wujiePower()&&!preview&&state.valid&&state.usbPriorityStatus&&state.usbPriorityStatus->writable&&ec&&model.ecVerified()&&(!busy||(reading&&!model.hasQueuedCommand()))&&!deviceOperations().interactiveBusy();
        for(auto button:usbPriorityButtons)EnableWindow(button,usbPriorityReady);
        refreshKeyboardIdleReadout();
        struct Finish{Impl& self;~Finish(){self.syncBatteryCard();}} finish{*this};
        ShowWindow(customColorButton,visibleCards[2]&&ui::customKeyboardColorVisible(model.effectIndex())?SW_SHOWNA:SW_HIDE);
        syncLightingTitle();
        // Do not disable and re-enable these on every refresh: that cancels a
        // mouse press if the refresh lands between button-down and button-up.
        const auto enableCustomPresets=[this](bool enabled){for(auto button:customPresetButtons)
            if((IsWindowEnabled(button)!=FALSE)!=enabled)EnableWindow(button,enabled);};
        windows::BatteryEditorInput editorInput{preview,busy,reading,deviceOperations().interactiveBusy(),bool(nativeBattery),
            bool(oemControl),bool(ec),gm5Ui,state.nativeBattery.percentageWritable,
            state.oem.chargeWritable&&model.honorBatteryBackend(),state.valid,loggerObservation.has_value()};
        const auto logger=windows::batteryEditorAvailability(editorInput);
        for(auto h:{loggerAddress.edit,loggerAddress.spin})EnableWindow(h,logger.address);
        for(auto h:{loggerValue.edit,loggerValue.spin})EnableWindow(h,logger.value);
        editorInput.interactiveBusy=deviceOperations().interactiveBusy();
        const auto restart=windows::batteryEditorAvailability(editorInput);
        EnableWindow(restartEdit,restart.restart);EnableWindow(restartSpin,restart.restart);
        SendMessageW(restartSpin,UDM_SETRANGE32,1,restart.restartMaximum);
        if(!restartDelay.pending&&!(busy&&!reading)&&GetFocus()!=restartEdit&&GetFocus()!=restartSpin&&!SendMessageW(restartEdit,EM_GETMODIFY,0,0)){
            const auto lower=nativeBattery?(state.nativeBattery.restartRaw?std::optional<int>(*state.nativeBattery.restartRaw&0x7f):std::nullopt):
                oemControl?state.oem.chargeStart:state.ecBattery.restartRaw?std::optional<int>(*state.ecBattery.restartRaw&0x7f):std::nullopt;
            if(lower&&*lower>0&&*lower<=99)SendMessageW(restartSpin,UDM_SETPOS32,0,*lower);
        }
        for(int i=0;i<6;++i){const auto field=powerOption(i);const bool enabled=!preview&&state.oem.connected&&state.oem.customCpuPowerWritable&&extendedPower()&&powerEditing&&(!busy||reading)&&!deviceOperations().interactiveBusy()&&field&&field->writable;
            if(field)SendMessageW(powerOptions[i].spin,UDM_SETRANGE32,field->minimum,field->maximum);
            EnableWindow(powerOptions[i].edit,enabled);EnableWindow(powerOptions[i].spin,enabled);}
        if(preview){
            for(auto h:batteryButtons)EnableWindow(h,TRUE);
            for(int i=0;i<5;++i)EnableWindow(performanceButtons[i],i>0&&i<=stockCpuProfileCount(deviceProfile(pageBoard).id));
            for(auto h:effectButtons)EnableWindow(h,TRUE);
            for(auto h:colorButtons)EnableWindow(h,TRUE);
            for(int i=0;i<5;++i)EnableWindow(levelButtons[i],i<=model.brightnessMaximum());
            EnableWindow(manualPowerButton,TRUE);enableCustomPresets(true);
            for(auto h:{customColorButton,nativeButton,keyboardTimeoutCheck,keyboardTimeoutEdit,keyboardTimeoutSpin,apuPolicySwitch[0],apuPolicySwitch[1],apuPolicySwitch[2]})EnableWindow(h,FALSE);
            for(int i=0;i<3;++i){EnableWindow(powerEdits[i],powerEditing);EnableWindow(powerSpins[i],powerEditing);}
            // WMI ranges are device readback and do not exist in a preview.
            for(auto& f:powerOptions){EnableWindow(f.edit,powerEditing);EnableWindow(f.spin,powerEditing);}
            return;
        }
        if(gm6Controller){const bool idle=(!busy||reading)&&!deviceOperations().interactiveBusy();const bool ready=idle&&state.gm6.connected&&state.gm6.writable;
            const auto power=windows::gmPowerButtonAvailability({idle,state.gm6.connected,state.gm6.writable,state.gm6.pl4Double,
                gmPowerDoubleCodec(deviceProfile(pageBoard).id),state.gm6.cpuPresetWritable,powerEditing,
                {state.gm6.cpuPresetLimits[0].has_value(),state.gm6.cpuPresetLimits[1].has_value(),state.gm6.cpuPresetLimits[2].has_value()}},[]{return windows::apuOnBattery();});
            EnableWindow(performanceButtons[0],*power.modes[0]);EnableWindow(performanceButtons[4],*power.modes[4]);
            for(int i=1;i<4;++i)EnableWindow(performanceButtons[i],*power.modes[i]);
            EnableWindow(manualPowerButton,power.custom);enableCustomPresets(power.presets);
            if(power.cancelEditing){model.stopPowerEditing();renderSelection();}
            for(int i=0;i<3;++i){EnableWindow(powerEdits[i],power.editors);EnableWindow(powerSpins[i],power.editors);}
            const auto battery=windows::gmBatteryButtonAvailability(idle,model.hasQueuedCommand(),state.nativeBattery.percentageWritable,state.nativeBattery.enforcementWritable);
            for(int i=0;i<ui::batteryThresholdCount;++i)EnableWindow(batteryButtons[i],battery.percentage);
            for(std::size_t i=0;i<apuPolicySwitch.size();++i)EnableWindow(apuPolicySwitch[i],battery.policy[i]);
            updateLightingButtons(iteLighting?idle:ready,bool(iteLighting),true);
            for(auto b:{keyboardTimeoutCheck,keyboardTimeoutEdit,keyboardTimeoutSpin})EnableWindow(b,FALSE);
            return;
        }
        if(oemControl){
            const bool idle=(!busy||reading)&&!deviceOperations().interactiveBusy();
            const bool honorBattery=model.honorBatteryBackend();
            const auto battery=windows::honorBatteryButtonAvailability(idle,state.oem.chargeWritable&&honorBattery,state.oem.turboWritable&&honorBattery,[]{return windows::apuOnBattery();});
            for(int i=0;i<ui::batteryThresholdCount;++i)EnableWindow(batteryButtons[i],battery.percentage);
            for(std::size_t i=0;i<apuPolicySwitch.size();++i)EnableWindow(apuPolicySwitch[i],battery.policy[i]);
            const bool honorPower=model.honorPowerBackend();
            const auto power=honorPower?windows::honorPowerButtonAvailability({idle,state.apu.stale,state.oem.stockCpuProfileWritable,powerEditing,state.apu.tableVersion},[]{return windows::apuOnBattery();}):
                windows::nativePowerButtonAvailability({idle,state.oem.connected,state.oem.stockCpuProfileWritable,state.oem.customCpuPowerWritable,powerEditing,stockCpuProfileCount(deviceProfile(pageBoard).id)});
            EnableWindow(performanceButtons[0],*power.modes[0]);
            for(int i=1;i<3;++i)EnableWindow(performanceButtons[i],*power.modes[i]);
            if(!honorPower)for(int i=0;i<5;++i)EnableWindow(performanceButtons[i],
                idle&&state.oem.stockCpuProfileWritable&&i>=1&&i<=stockCpuProfileCount(deviceProfile(pageBoard).id));
            EnableWindow(manualPowerButton,power.custom);enableCustomPresets(power.presets);
            synchronizingPower=true;
            for(int i=0;i<3;++i){if((honorPower||model.nativePowerBackend())&&state.oem.connected)SendMessageW(powerSpins[i],UDM_SETRANGE32,model.customMinimum(i),model.customMaximum(i));EnableWindow(powerEdits[i],power.editors);EnableWindow(powerSpins[i],power.editors);}
            synchronizingPower=false;
            for(auto button:effectButtons)EnableWindow(button,FALSE);
            for(auto button:colorButtons)EnableWindow(button,FALSE);
            for(int level=0;level<5;++level)EnableWindow(levelButtons[level],idle&&state.oem.keyboardWritable&&level<=model.brightnessMaximum());
            for(auto control:{customColorButton,nativeButton,keyboardTimeoutCheck,keyboardTimeoutEdit,keyboardTimeoutSpin})EnableWindow(control,FALSE);
            // Honor retains its backend capability, but no longer exposes it here.
            return;
        }
        const bool nativeLock = model.apuLockedByFanDefault();
        if (nativeLock != lastFanDefaultLock) {
            lastFanDefaultLock = nativeLock;
            if (nativeLock) model.stopPowerEditing();
            renderSelection();
        }
        const BOOL enabled = state.valid && (!busy || (reading && !model.hasQueuedCommand())) && ec && model.ecVerified() && !deviceOperations().interactiveBusy();
        const auto enable = [enabled](HWND control) {
            if (IsWindowEnabled(control) != enabled) EnableWindow(control, enabled);
        };
        const auto battery=windows::ecBatteryButtonAvailability({enabled!=FALSE,bool(nativeBattery),state.valid,busy,reading,
            model.hasQueuedCommand(),deviceOperations().interactiveBusy(),state.nativeBattery.percentageWritable,state.nativeBattery.enforcementWritable});
        for (int i=0;i<ui::batteryThresholdCount;++i) {
            if(nativeBattery)EnableWindow(batteryButtons[i],battery.percentage);
            else enable(batteryButtons[i]);
        }
        // EC platform policy remains available in Default; only watt controls depend on FAN mode.
        for (std::size_t i=0;i<apuPolicySwitch.size();++i) {
            if(nativeBattery)EnableWindow(apuPolicySwitch[i],battery.policy[i]);
            else enable(apuPolicySwitch[i]);
        }
        const bool needsRestore = model.fanNeedsRestore();
        const auto power=windows::ecPowerButtonAvailability({enabled!=FALSE,nativeLock,needsRestore,model.wujiePower(),bool(controller),powerEditing});
        for (int i = 0; i < 4; ++i) {
            const BOOL ready = *power.modes[i];
            if (IsWindowEnabled(performanceButtons[i]) != ready) EnableWindow(performanceButtons[i], ready);
        }
        if(power.modes[4])EnableWindow(performanceButtons[4],*power.modes[4]);
        // WUJIE Custom remains5..255 on AC/DC; supply changes never rewrite its draft.
        const BOOL manualReady = power.custom;
        if (IsWindowEnabled(manualPowerButton) != manualReady) EnableWindow(manualPowerButton, manualReady);
        enableCustomPresets(power.presets);
        const BOOL manualEditorsEnabled = power.editors;
        for (int i = 0; i < 3; ++i) {
            if (IsWindowEnabled(powerEdits[i]) != manualEditorsEnabled) EnableWindow(powerEdits[i], manualEditorsEnabled);
            if (IsWindowEnabled(powerSpins[i]) != manualEditorsEnabled) EnableWindow(powerSpins[i], manualEditorsEnabled);
        }
        updateLightingButtons(enabled!=FALSE,false,false);
        // The user requested an EC timeout retained after exit. No supported
        // register/command or current value has been established for this board.
        EnableWindow(keyboardTimeoutCheck,FALSE);
        EnableWindow(keyboardTimeoutEdit,FALSE);
        EnableWindow(keyboardTimeoutSpin,FALSE);
    }

    void invalidateBatteryStatus() const {
        // Project/Platform/ROM are painted separately in the card heading.
        // Accepted snapshot changes must repaint that heading too.
        const auto identity=toNativeRect(setsLayout.batteryIdentity);
        InvalidateRect(page,&identity,FALSE);
        const auto batteryDeviceIdentity=toNativeRect(setsLayout.batteryDeviceIdentity);
        InvalidateRect(page,&batteryDeviceIdentity,FALSE);
        const auto voltageSummary=toNativeRect(setsLayout.batteryVoltageSummary);
        InvalidateRect(page,&voltageSummary,FALSE);
        // Port/flow positions are fitted to the current measured text in paint().
        // Invalidating the three static fields separately leaves their 12-DIP
        // gaps outside the update region, where old glyph fragments can survive.
        // Repaint the complete row (including gaps), without moving any field.
        RECT left{}, row{};
        UnionRect(&left, &batteryPortDetail, &batteryFlowDetail);
        UnionRect(&row, &left, &batteryInfoDetail);
        row=toNativeRect(setsLayout.portDetail);
        InvalidateRect(page, &row, FALSE);
        // The same accepted sample drives the primary EC time estimates and
        // diagnostic battery card on every model. Their regions must also be
        // refreshed on passive reads, not just exposed by a scroll/mouse paint.
        // Keep this on the buffered content HWND; do not invalidate buttons or
        // the scrollbar, and do not erase the background between readings.
        for(const auto& card:setsLayout.batteryInfoCards){
            const auto bounds=toNativeRect(card);InvalidateRect(page,&bounds,FALSE);
        }
        if(batteryLoggingEnabled()){
            const auto diagnostic=toNativeRect(setsLayout.batteryComparison);
            InvalidateRect(page,&diagnostic,FALSE);
        }
    }
    void renderSelection() {
        const auto cards=windowsSettingsCards(deviceProfile(pageBoard),state.osBattery,state.ecBattery,state.batteryPolicy,batteryLoggingEnabled(),state.voltageBattery.targetVoltageMv);
        if(cards.visible!=visibleCards)arrange();
        const bool hasNativeModes=bool(voltageBattery)||state.oem.chargeModeWritable||state.oem.chargeMode.has_value();
        const int batterySelection=hasNativeModes&&batteryCardView!=ui::BatteryCardView::Capacity?-1:model.batteryIndex();
        for(int i=0;i<2;++i)if(customBaseButtons[i])setSelected(customBaseButtons[i],i==model.selectedCustomBaseValue());
        if(powerOptionRows()!=powerOptionRowsLaidOut){powerOptionRowsLaidOut=powerOptionRows();arrange();}
        if(!powerEditing)powerFieldsShowManual=false;
        select(usbPriorityButtons,model.wujiePower()&&state.valid&&state.usbPriorityStatus&&state.usbPriorityStatus->writable?((state.usbPriorityStatus->raw&ec::kTypeCPriorityBit)?1:0):-1);
        if(iteLighting&&!state.lighting.find(selectedLightingZone)&&!state.lighting.zones.empty())model.selectLightingZoneLocal(state.lighting.zones.front().zone);
        syncLightingTitle();
        const int platformPolicy=nativeBattery?ui::ecPolicySelection(state.nativeBattery.platformRaw,state.nativeBattery.romIdRaw):
            state.valid?ui::ecPolicySelection(state.batteryPlatform,state.batteryRom):-1;
        if(preview){
            const auto& p=model.previewState();
            select(performanceButtons,p.customEditing?-1:p.power);
            selectCustomButtons(p.customEditing);
            select(batteryButtons,p.batteryUpper);
            select(effectButtons,p.effect);select(colorButtons,p.color);select(levelButtons,p.brightness);
            if(powerEditing&&!powerFieldsShowManual){syncPowerFields(true);powerFieldsShowManual=true;}
            invalidateBatteryStatus();return;
        }
        syncBrightness();
        if(!ec&&!oemControl&&!gm6Controller){
            select(batteryButtons,nativeBattery?batterySelection:-1);select(apuPolicySwitch,platformPolicy);select(performanceButtons,-1);
            select(effectButtons,-1);select(colorButtons,-1);selectCustomButtons(false);
            invalidateBatteryStatus();return;
        }
        if(gm6Controller){const int selected=model.displayedPowerMode(true);select(performanceButtons,selected);selectCustomButtons(selected==model.customPowerIndex());
            if(!powerEditing||!powerFieldsShowManual)syncPowerFields(powerEditing);powerFieldsShowManual=powerEditing;
            select(batteryButtons,batterySelection);select(apuPolicySwitch,platformPolicy);
            if(pulsationPalette!=(!iteLighting&&model.effectIndex()==1)||cycleUi!=(model.effectIndex()==3))arrange();
            select(effectButtons,state.keyboardSupported?model.effectIndex():-1);select(colorButtons,state.keyboardSupported?paletteIndex():-1);
            for(const auto& r:cardDetails)InvalidateRect(page,&r,FALSE);for(const auto& r:apuDiagnostics)InvalidateRect(page,&r,FALSE);
            InvalidateRect(page,&manualPowerHint,FALSE);invalidateBatteryStatus();return;
        }
        if(oemControl){select(batteryButtons,batterySelection);select(apuPolicySwitch,state.oem.nativeTurbo?(*state.oem.nativeTurbo?1:0):-1);
            const int selected=model.displayedPowerMode(false);select(performanceButtons,selected);selectCustomButtons(powerEditing||selected==4);
            if(!powerEditing||!powerFieldsShowManual)syncPowerFields(powerEditing);powerFieldsShowManual=powerEditing;
            select(effectButtons,-1);select(colorButtons,-1);
            if(state.oem.keyboardTimeoutSeconds&&GetFocus()!=keyboardTimeoutEdit){const int seconds=*state.oem.keyboardTimeoutSeconds;
                SendMessageW(keyboardTimeoutSpin,UDM_SETPOS32,0,seconds);SendMessageW(keyboardTimeoutEdit,EM_SETMODIFY,FALSE,0);
                SendMessageW(keyboardTimeoutCheck,BM_SETCHECK,seconds?BST_CHECKED:BST_UNCHECKED,0);}
            for(const auto& detail:cardDetails)InvalidateRect(page,&detail,FALSE);
            for(const auto& detail:apuDiagnostics)InvalidateRect(page,&detail,FALSE);
            InvalidateRect(page,&manualPowerHint,FALSE);InvalidateRect(page,&apuPolicyCaption,FALSE);
            invalidateBatteryStatus();
            return;}
        if (pulsationPalette != (model.effectIndex() == 1)||cycleUi!=(model.effectIndex()==3)) arrange();
        select(batteryButtons, batterySelection);
        select(apuPolicySwitch,platformPolicy);
        // The KEYs policy switch shows Auto; SETs highlights actual battery upper limit.
        const bool nativeLock = model.apuLockedByFanDefault();
        if (nativeLock) model.stopPowerEditing();
        // Native CPU policy is independent of the selected FAN mode.
        const int selectedPower = model.displayedPowerMode(true);
        select(performanceButtons, selectedPower);
        selectCustomButtons( selectedPower == model.customPowerIndex());
        const bool showManual = powerEditing;
        // Passive SMU polling must never overwrite an in-progress manual edit.
        if (!showManual || !powerFieldsShowManual) syncPowerFields(showManual);
        powerFieldsShowManual = showManual;
        select(effectButtons, model.effectIndex());

        const int color = paletteIndex();
        select(colorButtons, color);
        setSelected(customColorButton, (model.effectIndex()==0||(iteLighting&&model.effectIndex()==1))&&color<0);
        for (const RECT& detail : cardDetails) InvalidateRect(page, &detail, FALSE);
        for (const RECT& detail : apuDiagnostics) InvalidateRect(page, &detail, FALSE);
        InvalidateRect(page, &manualPowerHint, FALSE);
        InvalidateRect(page, &apuPolicyCaption, FALSE);
        invalidateBatteryStatus();
    }






    void setSelected(HWND button, bool selected) {
        if ((GetPropW(button, L"MC.Selected") != nullptr) == selected) return;
        if (selected) SetPropW(button, L"MC.Selected", reinterpret_cast<HANDLE>(1));
        else RemovePropW(button, L"MC.Selected");
        InvalidateRect(button, nullptr, FALSE);
    }

    void selectCustomButtons(bool active){
        setSelected(manualPowerButton,active);
        for(int i=0;i<3;++i)setSelected(customPresetButtons[i],active&&i==model.highlightedCustomSlot());
    }
    template <std::size_t N>
    void select(const std::array<HWND, N>& buttons, int selected) {
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            setSelected(buttons[i], static_cast<int>(i) == selected);
        }
    }



    void commitLogger(bool writing){
        wchar_t address[32]{},value[32]{};GetWindowTextW(loggerAddress.edit,address,32);GetWindowTextW(loggerValue.edit,value,32);
        model.requestLogger(mc::windows::encoding::utf8(address),mc::windows::encoding::utf8(value),writing);
    }
    void deferRestart(std::uint64_t delay=ui::NumericCommitDelay::stepPauseMs){
        if(preview||closing)return;if(audit::capture())restartAuditId=audit::capture();restartDelay.schedule(GetTickCount64(),delay);
        SetTimer(page,kRestartApplyTimer,100,nullptr);
    }
    void commitRestart(bool force=false){
        audit::Scope trace("battery.restart_commit","",audit::capture()?audit::capture():restartAuditId,force||SendMessageW(restartEdit,EM_GETMODIFY,0,0)!=0);
        if(preview||closing||(!force&&!SendMessageW(restartEdit,EM_GETMODIFY,0,0)))return;
        if(busy||deviceOperations().interactiveBusy()){deferRestart(0);return;}
        restartDelay.cancel();KillTimer(page,kRestartApplyTimer);
        SendMessageW(restartEdit,EM_SETMODIFY,FALSE,0);
        BOOL valid{};const int value=GetDlgItemInt(page,kRestartEdit,&valid,FALSE);
        if(!valid||value<1||value>(nativeBattery?95:99)){model.setStatus(L"Неверный нижний порог зарядки");return;}
        requestedCommand=kRestartEdit;requestedNotice={"battery","Батарея","Применяю нижний порог…",osd::Icon::Battery};
        model.submit(requestedCommand,requestedNotice,windows::SettingsAction{windows::SettingsActionKind::BatteryRestart,value},L"Применяю нижний порог…",L"Нижний порог записан и сохранён.");
    }

    void commitCycleInterval(){BOOL valid{};const int seconds=GetDlgItemInt(page,kCycleEdit,&valid,FALSE);model.changeCycleInterval(valid?seconds:0);}
    void chooseCycleColor(int index){
        if(preview||busy||!cycle||index<0||index>=6)return;const auto previous=cycleSettings.colors[index];
        CHOOSECOLORW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetAncestor(page,GA_ROOT);
        dialog.lpCustColors=customColors.data();dialog.Flags=CC_FULLOPEN|CC_RGBINIT;dialog.rgbResult=RGB(previous[0],previous[1],previous[2]);
        if(!control::chooseColor(dialog))return;
        model.changeCycleColor(index,{GetRValue(dialog.rgbResult),GetGValue(dialog.rgbResult),GetBValue(dialog.rgbResult)});
    }
    void chooseCustomColor() {
        if (preview || (busy && !reading) || (!ec&&!gm6Controller&&!iteLighting) || model.effectIndex()==1 || (!iteLighting&&model.effectIndex() != 0)) return;
        CHOOSECOLORW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = GetAncestor(page, GA_ROOT);
        dialog.lpCustColors = customColors.data();
        dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
        const int preset = paletteIndex();
        const auto previewChannel = [](std::uint8_t value) {
            return static_cast<std::uint8_t>(std::min(MulDiv(value, 255, 50), 255));
        };
        dialog.rgbResult = preset >= 0 ? displayColor(preset) :
            rgb(previewChannel(state.color[0]), previewChannel(state.color[1]), previewChannel(state.color[2]));
        if (!control::chooseColor(dialog)) return;
        const COLORREF selectedColor = dialog.rgbResult;
        requestedCommand=kCustomColor;
        requestedNotice=model.lightingNotice(std::format("#{:02X}{:02X}{:02X}",GetRValue(selectedColor),GetGValue(selectedColor),GetBValue(selectedColor)));
        model.submit(requestedCommand,requestedNotice,windows::SettingsAction::colorRequest({GetRValue(selectedColor),GetGValue(selectedColor),GetBValue(selectedColor)}), L"Применяю свой цвет…");
    }
    void drawButton(const DRAWITEMSTRUCT& item) {
        const int controlId = static_cast<int>(item.CtlID);
        if(controlId==kLightingZoneTitle||controlId==kBatteryCardTitle){control::ButtonStyle style;style.background=theme::kSurface;style.border=false;style.segment=true;style.alignment=DT_LEFT;style.focusIndicator=false;
            control::drawButton(item,bold,dpi,false,style);return;}
        const bool selected = GetPropW(item.hwndItem, L"MC.Selected") != nullptr;
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;

        if (controlId >= kColorFirst && controlId < kColorFirst + static_cast<int>(colorValues().size())) {
            if(pulsationPalette&&model.pulsePalette().numbered){control::ButtonStyle style;style.selectedBorder=true;style.highlightDisabledSelection=selected;control::drawButton(item,body,dpi,selected,style);return;}
            const COLORREF color = displayColor(controlId - kColorFirst);
            if (disabled) {
                const int surfaceRed = static_cast<int>(theme::kSurface & 0xFFu);
                const int surfaceGreen = static_cast<int>((theme::kSurface >> 8) & 0xFFu);
                const int surfaceBlue = static_cast<int>((theme::kSurface >> 16) & 0xFFu);
                const int red = (GetRValue(color) + 2 * surfaceRed) / 3;
                const int green = (GetGValue(color) + 2 * surfaceGreen) / 3;
                const int blue = (GetBValue(color) + 2 * surfaceBlue) / 3;
                const COLORREF muted = static_cast<COLORREF>((red << 16) | (green << 8) | blue);
                fill(item.hDC, item.rcItem, muted);
            } else {
                fill(item.hDC, item.rcItem, color);
            }
            RECT border = item.rcItem;
            const HBRUSH brush = CreateSolidBrush(selected ? theme::kAccentHover : theme::kDivider);
            FrameRect(item.hDC, &border, brush);
            DeleteObject(brush);
            if (item.itemState & ODS_FOCUS) {
                InflateRect(&border, -scale(3), -scale(3));
                DrawFocusRect(item.hDC, &border);
            }
            return;
        }

        control::ButtonStyle style;
        const bool agingButton=item.CtlID==kBatteryAgingOff||item.CtlID==kBatteryAgingOn;
        style.highlightDisabledSelection = (gm6Controller||agingButton) && selected;
        style.selectedBorder = true;
        // Selection comes only from the CPU snapshot/pending CPU action.
        // FAN Default must never fabricate a second selected power profile.
        control::drawButton(item, body, dpi, selected, style);
    }

    // Separate opaque scrollbar, same layout principle as KEYs. Content scrolling
    // cannot copy pixels over the thumb or leave a detached strip beside cards.
    static LRESULT CALLBACK scrollbarProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT ps{}; const HDC dc = BeginPaint(window, &ps);
            RECT client{}; GetClientRect(window, &client); fill(dc, client, theme::kBackground);
            SetViewportOrgEx(dc, -(self->width - self->scrollLane()), 0, nullptr);
            if (self->scroll.scrollable()) self->drawThumb(dc);
            EndPaint(window, &ps); return 0;
        }
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSEMOVE) {
            const auto mapped = MAKELPARAM(GET_X_LPARAM(lp) + self->width - self->scrollLane(), GET_Y_LPARAM(lp));
            const auto result = SendMessageW(self->viewport, message, wp, mapped);
            InvalidateRect(window, nullptr, FALSE); return result;
        }
        if (message == WM_MOUSEWHEEL || message == WM_MOUSELEAVE) {
            const auto result = SendMessageW(self->viewport, message, wp, lp);
            InvalidateRect(window, nullptr, FALSE); return result;
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    static LRESULT CALLBACK viewportProcedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wp, lp);
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; const HDC dc = BeginPaint(window, &ps);
            RECT client{}; GetClientRect(window, &client); fill(dc, client, theme::kBackground);
            EndPaint(window, &ps); return 0;
        }
        case WM_MOUSEWHEEL: self->wheel(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
        case WM_KEYDOWN: {
            int position = self->scroll.position();
            switch (wp) {
            case VK_UP: position += self->scroll.lineStep(false, self->scale(24)); break;
            case VK_DOWN: position += self->scroll.lineStep(true, self->scale(24)); break;
            case VK_PRIOR: position += self->scroll.pageStep(false); break;
            case VK_NEXT: position += self->scroll.pageStep(true); break;
            case VK_HOME: position = 0; break;
            case VK_END: position = self->scroll.maximumPosition(); break;
            default: return DefWindowProcW(window, message, wp, lp);
            }
            self->scrollTo(position); return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!self->scroll.scrollable() || GET_X_LPARAM(lp) < self->width - self->scrollLane()) break;
            SetFocus(window);
            const int y = GET_Y_LPARAM(lp); const RECT thumb = self->thumb();
            page::scrollPress(window,self->scroll,thumb,y,self->scrollDragging,self->scrollGrabOffset,[self](int position){self->scrollTo(position);});
            InvalidateRect(window, nullptr, FALSE); return 0;
        }
        case WM_LBUTTONUP:
            if (self->scrollDragging) { ReleaseCapture(); return 0; }
            break;
        case WM_CAPTURECHANGED:
            self->scrollDragging = false; InvalidateRect(window, nullptr, FALSE); InvalidateRect(self->scrollbar, nullptr, FALSE); return 0;
        case WM_MOUSEMOVE: {
            if (self->scrollDragging) {
                const auto thumb = self->thumb();
                page::scrollDrag(self->scroll,thumb,self->height,self->dpi,GET_Y_LPARAM(lp),self->scrollGrabOffset,[self](int position){self->scrollTo(position);});
            }
            const bool hot = GET_X_LPARAM(lp) >= self->width - self->scrollLane();
            if (hot != self->scrollHot) { self->scrollHot = hot; InvalidateRect(window, nullptr, FALSE); InvalidateRect(self->scrollbar, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track); return 0;
        }
        case WM_MOUSELEAVE:
            self->scrollHot = false; InvalidateRect(window, nullptr, FALSE); InvalidateRect(self->scrollbar, nullptr, FALSE); return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_TIMER:
            if(wParam==kRestartApplyTimer){
                if(!self->restartDelay.pending)KillTimer(window,kRestartApplyTimer);
                else if(self->restartDelay.ready(GetTickCount64())&&!self->busy&&!deviceOperations().interactiveBusy())self->commitRestart(true);
                return 0;
            }
            if (wParam == kRefreshTimer) {self->refreshKeyboardIdleReadout();self->model.readState(true);}
            return 0;
        case WM_PAINT: self->paint(); return 0;
        case WM_CTLCOLORSTATIC: {
            if (const auto brush = numericFieldColors(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam))) return reinterpret_cast<LRESULT>(brush);
            HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, theme::kTextSecondary); SetBkColor(dc, theme::kBackground); return reinterpret_cast<LRESULT>(self->background);
        }
        case WM_CTLCOLOREDIT: {
            if (const auto brush = numericFieldColors(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam))) return reinterpret_cast<LRESULT>(brush);
            HDC dc=reinterpret_cast<HDC>(wParam);
            SetTextColor(dc,theme::kTextPrimary); SetBkColor(dc,theme::kSurfaceRaised);
            return reinterpret_cast<LRESULT>(self->numericBackground);
        }
        case WM_DRAWITEM: self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam)); return TRUE;
        case kCycleCommit:self->commitCycleInterval();return 0;
        case WM_NOTIFY:{const auto* h=reinterpret_cast<NMHDR*>(lParam);
            if(h&&h->idFrom>=kPowerOptionSpinFirst&&h->idFrom<kPowerOptionSpinFirst+6&&h->code==UDN_DELTAPOS){const auto* change=reinterpret_cast<NMUPDOWN*>(lParam);self->stepPowerOption(static_cast<int>(h->idFrom)-kPowerOptionSpinFirst,change->iDelta);return TRUE;}
            if(h&&h->idFrom==kCycleSpin&&h->code==UDN_DELTAPOS){PostMessageW(window,kCycleCommit,0,0);return 0;}
            if(h&&h->idFrom==kRestartSpin&&h->code==UDN_DELTAPOS){
                SendMessageW(self->restartEdit,EM_SETMODIFY,FALSE,0);self->deferRestart();return 0;}break;}
        case WM_COMMAND:
            if(LOWORD(wParam)==kKeyboardTimeoutEdit||LOWORD(wParam)==kKeyboardTimeoutSpin||LOWORD(wParam)==kKeyboardTimeoutCheck)return 0;
            if(LOWORD(wParam)>=kBatteryVoltageFirst&&LOWORD(wParam)<kBatteryVoltageFirst+3){if(HIWORD(wParam)==BN_CLICKED)if(IsWindowEnabled(self->voltageButtons[LOWORD(wParam)-kBatteryVoltageFirst]))self->model.applyVoltageMode(LOWORD(wParam)-kBatteryVoltageFirst);return 0;}
            if(LOWORD(wParam)==kBatteryAgingOff||LOWORD(wParam)==kBatteryAgingOn){if(HIWORD(wParam)==BN_CLICKED&&IsWindowEnabled(self->agingButtons[LOWORD(wParam)==kBatteryAgingOn]))self->model.applyBatteryAging(LOWORD(wParam)==kBatteryAgingOn);return 0;}
            if(LOWORD(wParam)==kRestartEdit||LOWORD(wParam)==kRestartSpin)return 0;
            if(LOWORD(wParam)==kRestartEdit&&HIWORD(wParam)==EN_CHANGE&&GetFocus()==self->restartEdit&&SendMessageW(self->restartEdit,EM_GETMODIFY,0,0)){
                self->restartDelay.cancel();KillTimer(window,kRestartApplyTimer);return 0;}
            if(LOWORD(wParam)==kRestartEdit&&HIWORD(wParam)==EN_KILLFOCUS){if(!self->restartDelay.pending)self->commitRestart();return 0;}
            if(self->nativeBattery&&!self->gm6Controller&&HIWORD(wParam)==BN_CLICKED){const int id=LOWORD(wParam);
                if((id<kBatteryFirst||id>=kBatteryFirst+ui::batteryThresholdCount)&&id!=kCustomBaseZero&&id!=kCustomBaseTurbo&&id!=kApuPolicyOff&&id!=kApuPolicyOn&&id!=kApuPolicyOriginal)return 0;
                if(const auto target=GetDlgItem(window,id);!target||!IsWindowEnabled(target))return 0;
            }
            if(self->gm6Controller&&HIWORD(wParam)==BN_CLICKED){const int id=LOWORD(wParam);
                if(id!=kCustomBaseZero&&id!=kCustomBaseTurbo&&id!=kApuPolicyOff&&id!=kApuPolicyOn&&id!=kApuPolicyOriginal&&id!=kManualPower&&!customPresetCommand(id)&&!(id>=kBatteryFirst&&id<kBatteryFirst+ui::batteryThresholdCount)&&(id<kPerformanceFirst||id>=kPerformanceFirst+self->powerButtonCount())&&!(id>=kEffectFirst&&id<kEffectFirst+4)&&!(id>=kColorFirst&&id<kColorFirst+8)&&!(id>=kLevelFirst&&id<kLevelFirst+5)&&id!=kCustomColor&&id!=kNativeMode&&id!=kLightingZoneTitle)return 0;
                if(const auto target=GetDlgItem(window,id);target&&!IsWindowEnabled(target))return 0;
            }
            if(LOWORD(wParam)==kCycleEdit&&HIWORD(wParam)==EN_KILLFOCUS){self->commitCycleInterval();return 0;}
            if(self->oemControl&&LOWORD(wParam)==kKeyboardTimeoutEdit&&HIWORD(wParam)==EN_KILLFOCUS&&SendMessageW(self->keyboardTimeoutEdit,EM_GETMODIFY,0,0)){
                SendMessageW(self->keyboardTimeoutEdit,EM_SETMODIFY,FALSE,0);
                if(!self->state.oem.keyboardTimeoutWritable)return 0;
                BOOL valid{};const int seconds=GetDlgItemInt(window,kKeyboardTimeoutEdit,&valid,FALSE);
                if(!valid||seconds<0||seconds>3600){self->model.setStatus(L"Huawei: таймаут 0–3600 секунд; 0 — без выключения.");return 0;}
                self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::LightingTimeout,seconds},L"Применяю таймаут подсветки Huawei…");return 0;
            }
            if(LOWORD(wParam)>=kPowerOptionEditFirst&&LOWORD(wParam)<kPowerOptionEditFirst+6&&HIWORD(wParam)==EN_KILLFOCUS){self->commitPowerOption(LOWORD(wParam)-kPowerOptionEditFirst);return 0;}
            if (LOWORD(wParam) >= kPowerEditFirst && LOWORD(wParam) < kPowerEditFirst + 3 && HIWORD(wParam) == EN_KILLFOCUS) {
                self->commitPowerField(LOWORD(wParam) - kPowerEditFirst); return 0;
            }
            if (HIWORD(wParam) != BN_CLICKED || (self->busy && (!self->reading || self->model.hasQueuedCommand()))) return 0;
            self->requestedNotice = self->notice(LOWORD(wParam));
            self->requestedCommand = LOWORD(wParam);
            switch (LOWORD(wParam)) {
            case kCustomBaseZero:case kCustomBaseTurbo:self->model.setCustomBase(LOWORD(wParam)==kCustomBaseTurbo?1:0);return 0;
            case kLightingZoneTitle:self->chooseLightingZone();return 0;
            case kKeyboardTimeoutCheck:
                if(self->oemControl&&self->state.oem.keyboardTimeoutWritable){
                    BOOL valid{};int seconds=GetDlgItemInt(window,kKeyboardTimeoutEdit,&valid,FALSE);
                    if(SendMessageW(self->keyboardTimeoutCheck,BM_GETCHECK,0,0)!=BST_CHECKED)seconds=0;
                    else if(!valid||seconds<=0||seconds>3600)seconds=60;
                    self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::LightingTimeout,seconds},L"Применяю таймаут подсветки Huawei…");
                }return 0;
            case kApuPolicyOff: case kApuPolicyOn: case kApuPolicyOriginal: {
                const bool on = LOWORD(wParam) == kApuPolicyOn;
                const auto policy=LOWORD(wParam)==kApuPolicyOriginal?BatteryEcPolicy::Platform7Original:on?BatteryEcPolicy::Platform7Percent:BatteryEcPolicy::Platform4Original;
                if(policy==BatteryEcPolicy::Platform7Original&&!nativeEcPolicyProfile(deviceProfile(self->pageBoard).id))return 0;
                self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::BatteryPolicy,0,on,policy},
                    self->oemControl?L"Переключаю штатный Turbo Huawei…":L"Применяю политику EC…");
                return 0;
            }
            case kUsbPriorityBattery: case kUsbPriorityPerformance:
                self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::UsbPowerPriority,LOWORD(wParam)==kUsbPriorityPerformance?1:0},L"Применяю приоритет USB-C…",L"Бит приоритета подтверждён EC; физический эффект не проверен.");return 0;
            case kManualPower:
                self->model.applyPowerFromFields(); return 0;
            case kCustomColor: self->chooseCustomColor(); return 0;
            case kNativeMode: self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::LightingNative}, L"Применяю предустановку подсветки…"); return 0;
            default: break;
            }
            if(customPresetCommand(LOWORD(wParam))){self->model.selectCustomPreset(LOWORD(wParam)-kCustomPresetFirst);return 0;}
            if (LOWORD(wParam) >= kBatteryFirst && LOWORD(wParam) <= kBatteryAutomatic) {
                const int id = LOWORD(wParam) - kBatteryFirst; self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::BatteryUpper,id},L"Применяю лимит батареи…"); return 0;
            }
            if (LOWORD(wParam) >= kPerformanceFirst && LOWORD(wParam) < kPerformanceFirst + self->powerButtonCount()) {
                const int id = LOWORD(wParam) - kPerformanceFirst;
                // Minimal (0) and Auto (4) are application commands too. The
                // resource owner, not a stock-only UI range, admits them.
                if (id>0&&id<4&&id>stockCpuProfileCount(deviceProfile(self->pageBoard).id)) return 0;
                self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::StockPower,id}, L"Применяю режим производительности…"); return 0;
            }
            if (LOWORD(wParam) >= kEffectFirst && LOWORD(wParam) < kEffectFirst + 4) {
                if(self->iteLighting&&LOWORD(wParam)==kEffectFirst+3){self->chooseHidEffect();return 0;}
                const int id = LOWORD(wParam) - kEffectFirst; self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::LightingEffect,id}, L"Применяю режим подсветки…"); return 0;
            }
            if (LOWORD(wParam) >= kColorFirst && LOWORD(wParam) < kColorFirst + 8) {
                const int id = LOWORD(wParam) - kColorFirst; if(self->model.effectIndex()==3){self->chooseCycleColor(id);return 0;} self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::LightingColor,id}, L"Применяю цвет подсветки…"); return 0;
            }
            if (LOWORD(wParam) >= kLevelFirst && LOWORD(wParam) < kLevelFirst + 5) {
                const int id = LOWORD(wParam) - kLevelFirst; if(id>self->model.brightnessMaximum()||!IsWindowEnabled(self->levelButtons[id]))return 0; self->model.submit(self->requestedCommand,self->requestedNotice,windows::SettingsAction{windows::SettingsActionKind::LightingLevel,id}, L"Применяю яркость подсветки…"); return 0;
            }
            break;
        case WM_APP + 10: self->model.complete(); return 0;
        case WM_DESTROY: return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};

SettingsPage::SettingsPage(HWND parent, EcAccess* ec, OsdPopup* osd, std::function<void(std::string)> log,
                           std::shared_ptr<windows::FanController> controller,SettingsDispatch::ThreadFactory factory)
    :impl_(std::make_unique<Impl>(parent,ec,osd,std::move(log),std::move(controller),std::move(factory))){}
SettingsPage::~SettingsPage() = default;
void SettingsPage::arrange(const RECT& area, UINT dpi, bool visible) {
    auto& p = *impl_; const bool wasVisible = p.visible; p.visible = visible;
    if (dpi != p.dpi || p.fontLanguage != ui::languageIndex()) { p.dpi = dpi; p.createFonts(); }
    if (!visible) { KillTimer(p.page, kRefreshTimer); ShowWindow(p.viewport, SW_HIDE); return; }
    p.width = area.right - area.left; p.height = area.bottom - area.top;
    page::place(p.viewport, area.left, area.top, p.width, p.height, true);
    p.arrange();
    if (!p.loaded || !wasVisible) p.model.readState();
    p.updateButtons();
    SetTimer(p.page, kRefreshTimer, p.model.wujiePower()?1000:3000, nullptr);
    page::present(p.viewport);
}
bool SettingsPage::preTranslate(MSG& message) {
    auto& p = *impl_;
    if (!p.visible) return false;
    if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE&&p.powerEditing&&!p.busy&&
       (message.hwnd==p.viewport||IsChild(p.viewport,message.hwnd))){
        if(p.model.cancelCustomEditor()){SetFocus(p.viewport);return true;}
    }
    if(ui::showEcLoggerCard&&message.message==WM_KEYDOWN&&message.wParam==VK_RETURN&&(message.hwnd==p.loggerAddress.edit||message.hwnd==p.loggerAddress.spin||message.hwnd==p.loggerValue.edit||message.hwnd==p.loggerValue.spin)){
        p.commitLogger(message.hwnd==p.loggerValue.edit||message.hwnd==p.loggerValue.spin);SetFocus(p.viewport);return true;}
    if(releaseNumericFieldFocusWindows(message,p.viewport))return true;
    if (message.message == WM_MOUSEWHEEL &&
        (message.hwnd == p.viewport || IsChild(p.viewport, message.hwnd))) {
        p.wheel(GET_WHEEL_DELTA_WPARAM(message.wParam)); return true;
    }
    if (message.message == WM_KEYDOWN && message.hwnd == p.viewport) {
        switch (message.wParam) {
        case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
            SendMessageW(p.viewport, WM_KEYDOWN, message.wParam, message.lParam); return true;
        }
    }
    const bool handled = IsDialogMessageW(p.viewport, &message) != FALSE;
    if (handled && message.message == WM_KEYDOWN) p.revealFocus();
    return handled;
}
void SettingsPage::observeKeyboardLevel(int level) { if(impl_)impl_->model.observeKeyboardLevel(level); }
#ifdef MAGICCONTROL_WINDOWS_SETTINGS_TEST
void SettingsPage::testSeedBatteryProjection(const BatteryReading& sample){
    SettingsOperationsTestAccess::seed(impl_->model,sample);
}
bool SettingsPage::testBatteryProjectionAvailable() const{
    return impl_->batteryResources.view().available();
}
#endif
TraySettingsState SettingsPage::trayState() const {return impl_->model.trayState();}
void SettingsPage::refreshForTray(bool invalidateObservation){impl_->model.refreshForTray(invalidateObservation);}
void SettingsPage::refreshCommandAvailability(){if(impl_->model.observePowerSupply())InvalidateRect(impl_->page,nullptr,FALSE);impl_->updateButtons();}
void SettingsPage::applyFromTray(TraySetting setting, int value) {
    const int maximum = setting == TraySetting::Battery ? ui::batteryAutomaticIndex : setting == TraySetting::Brightness ? 4 : setting == TraySetting::Performance ? impl_->powerButtonCount()-1 : 2;
    const auto state = impl_->model.trayState();
    if (value < 0 || value > maximum || !state.ready ||
        (setting == TraySetting::Brightness && !state.keyboardSupported) ||
        (setting == TraySetting::Performance && !state.performanceReady) ||
        (setting == TraySetting::Performance && value == 3 && !state.turboAvailable)) return;
    const int first = setting == TraySetting::Battery ? kBatteryFirst :
        setting == TraySetting::Performance ? kPerformanceFirst : kLevelFirst;
    if(setting==TraySetting::Battery){impl_->model.useCapacityViewForTray();impl_->arrange();impl_->updateButtons();}
    SendMessageW(impl_->page, WM_COMMAND, MAKEWPARAM(first + value, BN_CLICKED), 0);
}
void SettingsPage::restoreKeyboardCycle(){impl_->model.requestCycleRestore();}
}
