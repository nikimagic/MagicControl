#include "MagicControl/ActionLog.h"
#include "MagicControl/DisplayContinuity.h"
#include "MagicControl/ResourceCatalog.h"
#include "MagicControl/ControlAudit.h"
#include "LocalizationWindows.h"
#include "ControlTheme.h"
#include "FansPage.h"
#include "../../platform/windows/FanCurveStorageWindows.h"
#include "../../platform/windows/FanDeviceActionsWindows.h"
#include "../../platform/windows/FanMonitoringWindows.h"
#include "../common/FooterStatus.h"
#include "../common/DefaultFanGraph.h"
#include "PageMechanics.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "../../devices/mechrevo/gm6ir0c/FactoryCurves.h"
#include "MagicControl/BoardIdentity.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/CustomPresets.h"

#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <format>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "MagicControl/EcAccess.h"
#include "MagicControl/EcRegister.h"
#include "MagicControl/FanCurve.h"
#include "MagicControl/FanControlLimits.h"
#include "../../platform/windows/FanControlWindows.h"
#include "../../platform/windows/NativeFixedFans.h"
#include "../../protocols/minisforum/HpbsdFans.h"
#include "../../platform/windows/PowerDiagnosticsWindows.h"
#include "MagicControl/FanTelemetry.h"
#include "MagicControl/Monitoring.h"
#include "MagicControl/Logging.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/HardwareInventory.h"
#include "MagicControl/ScrollModel.h"
#include "Layout.h"
#include "../common/Theme.h"
#include "OsdPopup.h"
#include "../common/OsdMessages.h"
#include "../../platform/windows/TextWindows.h"
#include "NumericFieldTheme.h"
#include "../common/MonitoringCards.h"
#include "../common/DeviceHeader.h"

namespace mc::gui {
namespace {

constexpr wchar_t kPageClass[] = L"MagicControl.FansPage";
constexpr UINT_PTR kSensorTimer = 1;
constexpr UINT_PTR kScrollMetricTimer = 72;
constexpr UINT_PTR kProfileSaveTimer = 75;
constexpr UINT kSensorsFinished = WM_APP + 61;
constexpr UINT kWorkerFinished = WM_APP + 60;
constexpr UINT kLinkCurves = 301;
constexpr UINT kEcManual = 302;
constexpr UINT kEcManualGpu = 303;
constexpr UINT kCpuPrevious = 310;
constexpr UINT kCpuNext = 311;
constexpr UINT kGpuPrevious = 312;
constexpr UINT kGpuNext = 313;
constexpr int kPresetBase = 320;
constexpr int kPresetCount = static_cast<int>(mc::kFanCurvePresetOrder.size());
constexpr int kEditBase = 400;
constexpr int kSpinBase = 500;
constexpr int kSpinControlCount = 32;
constexpr UINT kPollIntervalMs = 1000;

using FanReadout = mc::FanReadout;

using CurveCell = mc::FanCurveCell;
using CurveTable = mc::FanCurveTable;

using NumericEditor=NumericFieldWindows;

using page::Buffer;

void fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void drawText(HDC dc, const wchar_t* value, RECT rect, HFONT font, COLORREF color,
              UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    const HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    ::mc::gui::localizedDrawText(dc, value, -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, previous);
}

void drawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    const HGDIOBJ previous = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, previous);
    DeleteObject(pen);
}

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"Ошибка операции EC.";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}

// Presentation selector only: the shared GPU loader resolves this active GDI
// output to its exact adapter. Never substitute GPU activity for display routing.
// Called by processorText on the window thread, with no HWND captured by workers.
std::string gpuDisplayDevice(HWND window) {
    if (!window || !IsWindow(window)) return {};
    const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info))) return {};
    return mc::windows::encoding::utf8(info.szDevice);
}

bool registerClass(const wchar_t* name, WNDPROC procedure) {
    WNDCLASSW klass{};
    klass.lpfnWndProc = procedure;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = name;
    klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (RegisterClassW(&klass) != 0) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

}  // namespace

struct FansPage::Impl : windows::FansOperationsWindows::ViewPort {
    windows::FansOperationsWindows model;
    decltype(windows::FansOperationsWindows::Presentation::visible) visible{model.presentation().visible};
    decltype(windows::FansOperationsWindows::Presentation::busy) busy{model.presentation().busy};
    decltype(windows::FansOperationsWindows::Presentation::draftEdited) draftEdited{model.presentation().draftEdited};
    decltype(windows::FansOperationsWindows::Presentation::nativeDefaultMode) nativeDefaultMode{model.presentation().nativeDefaultMode};
    decltype(windows::FansOperationsWindows::Presentation::nativeFanBoost) nativeFanBoost{model.presentation().nativeFanBoost};
    decltype(windows::FansOperationsWindows::Presentation::modeKnown) modeKnown{model.presentation().modeKnown};
    decltype(windows::FansOperationsWindows::Presentation::pollCount) pollCount{model.presentation().pollCount};
    decltype(windows::FansOperationsWindows::Presentation::cpu) cpu{model.presentation().cpu};
    decltype(windows::FansOperationsWindows::Presentation::gpu) gpu{model.presentation().gpu};
    decltype(windows::FansOperationsWindows::Presentation::monitoring) monitoring{model.presentation().monitoring};
    decltype(windows::FansOperationsWindows::Presentation::resourceBoard) resourceBoard{model.presentation().resourceBoard};
    decltype(windows::FansOperationsWindows::Presentation::resourceState) resourceState{model.presentation().resourceState};
    decltype(windows::FansOperationsWindows::Presentation::operationStatus) operationStatus{model.presentation().operationStatus};
    decltype(windows::FansOperationsWindows::Presentation::huawei) huawei{model.presentation().huawei};
    decltype(windows::FansOperationsWindows::Presentation::preview) preview{model.presentation().preview};
    decltype(windows::FansOperationsWindows::Presentation::gm6State) gm6State{model.presentation().gm6State};
    decltype(windows::FansOperationsWindows::Presentation::gm6DraftKnown) gm6DraftKnown{model.presentation().gm6DraftKnown};
    decltype(windows::FansOperationsWindows::Presentation::gm6Requested) gm6Requested{model.presentation().gm6Requested};
    decltype(windows::FansOperationsWindows::Presentation::gm6RequestedChannels) gm6RequestedChannels{model.presentation().gm6RequestedChannels};
    decltype(windows::FansOperationsWindows::Presentation::hlylState) hlylState{model.presentation().hlylState};
    decltype(windows::FansOperationsWindows::Presentation::hlylKnown) hlylKnown{model.presentation().hlylKnown};
    decltype(windows::FansOperationsWindows::Presentation::nativeCurves) nativeCurves{model.presentation().nativeCurves};
    decltype(windows::FansOperationsWindows::Presentation::hlylDraft) hlylDraft{model.presentation().hlylDraft};
    decltype(windows::FansOperationsWindows::Presentation::profiles) profiles{model.presentation().profiles};
    decltype(windows::FansOperationsWindows::Presentation::requestedHighlight) requestedHighlight{model.presentation().requestedHighlight};
    decltype(windows::FansOperationsWindows::Presentation::controlStatus) controlStatus{model.presentation().controlStatus};
    decltype(windows::FansOperationsWindows::Presentation::ec) ec{model.presentation().ec};
    decltype(windows::FansOperationsWindows::Presentation::gm6Controller) gm6Controller{model.presentation().gm6Controller};
    decltype(windows::FansOperationsWindows::Presentation::telemetryLog) telemetryLog{model.presentation().telemetryLog};
    using CurveCell=windows::FansOperationsWindows::CurveCell;
    using CurveTable=windows::FansOperationsWindows::CurveTable;
    static constexpr auto View=windows::FansOperationsWindows::View;
    static constexpr auto Page=windows::FansOperationsWindows::Page;
    static constexpr auto Presets=windows::FansOperationsWindows::Presets;
    static constexpr auto Manual=windows::FansOperationsWindows::Manual;
    static constexpr auto Link=windows::FansOperationsWindows::Link;
    static constexpr auto Immediate=windows::FansOperationsWindows::Immediate;

    HWND page{};
    HWND view{};
    HWND linkCurves{}, ecManualButton{}, ecManualGpuButton{};
    std::array<HWND, 4> pageButtons{};
    std::array<std::array<HWND, mc::kFanCurvePresetOrder.size()>, 2> presetButtons{};
    std::array<std::array<NumericEditor, 8>, 2> temperatureEditors{};
    std::array<std::array<NumericEditor, 8>, 2> dutyEditors{};

    HFONT bodyFont{};
    HFONT captionFont{};
    HFONT headingFont{};
    HFONT valueFont{};
    HFONT smallValueFont{};
    HFONT monitorHeaderFont{}, monitorCaptionFont{};
    HFONT fanTitleFont{}, fanMetricFont{}, fanSecondaryFont{}, fanSecondaryCompactFont{};
    HBRUSH editBrush{CreateSolidBrush(theme::kNumericBackground)};
    HBRUSH disabledEditBrush{CreateSolidBrush(theme::kNumericDisabledBackground)};
    Buffer buffer;
    mc::ScrollModel scroll;

    HWND owner{};
    UINT dpi{96};unsigned fontLanguage{~0u};
    int width{};
    int height{};
    int viewWidth{};
    bool hot{};
    bool dragging{};
    bool synchronizingEditors{};
    int grabOffset{};
    int cpuCurvePage{};
    int gpuCurvePage{};
    HWND wheelEditor{};
    int wheelRemainder{};
    std::chrono::steady_clock::time_point scrollBurstStart{};
    std::chrono::steady_clock::time_point lastScrollInput{};
    unsigned scrollInputEvents{};
    unsigned scrollPaintEvents{};
    double scrollPaintTotalMs{};
    double scrollPaintMaxMs{};
    std::uint64_t scrollDistancePixels{};
    int curveLabelWidth{};

    std::array<RECT, 2> fanCards{};
    std::array<RECT, 5> monitorCards{};
    std::array<RECT, 2> curveCards{};
    std::array<RECT, 2> graphCards{};
    std::array<RECT, 2> editorCards{};
    RECT noteRect{};
    int contentExtent{};
    int measuredWidth{};

    OsdPopup* popup{};
    Impl(HWND parent, mc::EcAccess* access, OsdPopup* popupWindow, std::function<void(std::string)> logger,
         std::shared_ptr<mc::windows::FanController> controller, std::function<mc::MonitoringSnapshot()> source)
        : model(*this,access,std::move(controller),std::move(source),std::move(logger),ui::presentationBoard(),ui::laptopPreview()),owner(parent),popup(popupWindow) {
        if(preview){MonitoringSnapshot seed;if(ui::discretePreviewModel()){seed.processorVendors=ui::discretePreviewVendors();seed.processorModels=ui::nvidiaPreviewProcessorModels();}
            model.configurePreviewMonitoring(std::move(seed),ui::discretePreviewModel());}
        INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_UPDOWN_CLASS};
        if (!InitCommonControlsEx(&commonControls))
            throw std::runtime_error("Cannot initialize the native Windows spin control.");
        if (!registerClass(kPageClass, pageProcedure))
            throw std::runtime_error("Cannot register FANs page window class.");
        page = CreateWindowExW(WS_EX_CONTROLPARENT, kPageClass, L"FANs",
                               WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr,
                               GetModuleHandleW(nullptr), this);
        if (!page) throw std::runtime_error("Cannot create FANs page.");

        view = CreateWindowExW(0, L"STATIC", L"FANs content",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_NOTIFY | WS_CLIPCHILDREN,
                               0, 0, 0, 0, page, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!view) throw std::runtime_error("Cannot create FANs viewport.");
        SetWindowSubclass(view, viewProcedure, 1, reinterpret_cast<DWORD_PTR>(this));

        createEditors();
        if(huawei)for(int channel=0;channel<2;++channel){
            SendMessageW(dutyEditors[channel][0].spin,UDM_SETRANGE32,mc::fanInput::percentMinimum,mc::fanInput::percentMaximum);
            SendMessageW(dutyEditors[channel][0].spin,UDM_SETPOS32,0,50);
        }
        linkCurves = makeButton(kLinkCurves, L"CPU≠GPU");
        ecManualButton = makeButton(kEcManual, L"Manual");
        ecManualGpuButton = makeButton(kEcManualGpu, L"Manual");
        pageButtons[0] = makeButton(kCpuPrevious, L"‹");
        pageButtons[1] = makeButton(kCpuNext, L"›");
        pageButtons[2] = makeButton(kGpuPrevious, L"‹");
        pageButtons[3] = makeButton(kGpuNext, L"›");
        for (int channel = 0; channel < 2; ++channel)
            for (int preset = 0; preset < kPresetCount; ++preset)
                presetButtons[channel][preset] = makeButton(kPresetBase + channel * kPresetCount + preset,
                    !nativeCurves&&mc::kFanCurvePresetOrder[preset] == mc::FanCurvePreset::Manual ? L"Manual\nonline" :
                    !nativeCurves&&mc::kFanCurvePresetOrder[preset] == mc::FanCurvePreset::Min ? L"Min\nonline" :
                    wide(std::string(mc::kFanCurvePresetNames[static_cast<std::size_t>(mc::kFanCurvePresetOrder[preset])])).c_str());
        createFonts();
        model.bindReady([window=page](bool sensors){PostMessageW(window,sensors?kSensorsFinished:kWorkerFinished,0,0);});
        model.initialize(); // Initial snapshot only; visibility never reloads EC tables.
    }

    ~Impl() {
        if (page) KillTimer(page, kSensorTimer);
        if (page) KillTimer(page, kProfileSaveTimer);
        model.shutdown();
        if (page) DestroyWindow(page);
        if (bodyFont) DeleteObject(bodyFont);
        if (captionFont) DeleteObject(captionFont);
        if (headingFont) DeleteObject(headingFont);
        if (valueFont) DeleteObject(valueFont);
        if (smallValueFont) DeleteObject(smallValueFont);
        if (monitorHeaderFont) DeleteObject(monitorHeaderFont);
        if (monitorCaptionFont) DeleteObject(monitorCaptionFont);
        for (HFONT font : {fanTitleFont, fanMetricFont, fanSecondaryFont, fanSecondaryCompactFont}) if (font) DeleteObject(font);
        if (editBrush) DeleteObject(editBrush);
        if (disabledEditBrush) DeleteObject(disabledEditBrush);
    }









    void cancelProfileSave() override {KillTimer(page,kProfileSaveTimer);}
    std::uint64_t beginNotice(const mc::gui::osd::Message& message) override {return popup?popup->beginOperation(message):0;}
    void completeNotice(std::uint64_t token,mc::gui::osd::Message message) override {if(popup&&token)popup->completeOperation(token,std::move(message));}
    void resetCurvePage(int channel) override {if(channel==0)cpuCurvePage=0;else gpuCurvePage=0;}
    void clampCurvePages() override {cpuCurvePage=std::min(cpuCurvePage,1);gpuCurvePage=std::min(gpuCurvePage,1);}
    void invalidate(unsigned flags) override {
        if(flags&View)InvalidateRect(view,nullptr,FALSE);
        if(flags&Page)InvalidateRect(page,nullptr,FALSE);
        if(flags&Presets)for(const auto& channel:presetButtons)for(auto button:channel){InvalidateRect(button,nullptr,FALSE);if(flags&Immediate)UpdateWindow(button);}
        if(flags&Manual)for(auto button:{ecManualButton,ecManualGpuButton}){InvalidateRect(button,nullptr,FALSE);if(flags&Immediate)UpdateWindow(button);}
        if(flags&Link){InvalidateRect(linkCurves,nullptr,FALSE);if(flags&Immediate)UpdateWindow(linkCurves);}
    }
    void monitoringLayoutChanged() override {if(visible&&fanCards[0].bottom-fanCards[0].top!=requiredFanHeight(viewWidth))layout();}
    HWND makeButton(int id, const wchar_t* caption) {
        HWND button = CreateWindowExW(0, L"BUTTON", caption,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      0, 0, 0, 0, view,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      GetModuleHandleW(nullptr), nullptr);
        if (button) SetWindowSubclass(button, controlProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        if (button) control::themeButton(button);
        return button;
    }

    void createEditors() {
        synchronizingEditors = true;
        for (int channel = 0; channel < 2; ++channel) {
            for (int column = 0; column < 8; ++column) {
                const int temperatureId = kEditBase + channel * 16 + column;
                const int dutyId = kEditBase + channel * 16 + 8 + column;
                const int temperatureSpinId = kSpinBase + channel * 16 + column * 2;
                const int dutySpinId = temperatureSpinId + 1;
                auto& temperature = temperatureEditors[channel][column];
                auto& duty = dutyEditors[channel][column];
                const auto& curve = profiles.current(channel);
                const CurveCell& temperatureCell = curve[static_cast<std::size_t>(column)];
                const CurveCell& dutyCell = curve[static_cast<std::size_t>(column)];
                temperature=createNumericFieldWindows(view,temperatureId,temperatureSpinId,mc::fanInput::temperatureMinimum,mc::fanInput::temperatureMaximum,mc::fanInput::temperature(temperatureCell.temperature),0);
                duty=createNumericFieldWindows(view,dutyId,dutySpinId,mc::fanInput::percentMinimum,mc::fanInput::percentMaximum,mc::fanInput::percent(dutyCell.duty),0);
                for(HWND control:{temperature.edit,temperature.spin,duty.edit,duty.spin})
                    if(!SetWindowSubclass(control,controlProcedure,1,reinterpret_cast<DWORD_PTR>(this)))
                        throw std::runtime_error("Cannot attach FANs numeric input handler.");
            }
        }
        synchronizingEditors = false;
    }

    int scale(int logical) const {
        return MulDiv(logical, static_cast<int>(dpi), 96);
    }

    int scrollLane() const { return scale(12); }

    void createFonts() {
        fontLanguage=ui::languageIndex();
        for (HFONT* font : {&bodyFont, &captionFont, &headingFont, &valueFont, &smallValueFont,
                            &monitorHeaderFont, &monitorCaptionFont, &fanTitleFont, &fanMetricFont, &fanSecondaryFont,
                            &fanSecondaryCompactFont}) {
            if (*font) DeleteObject(*font);
            *font = nullptr;
        }
        bodyFont = createUiFont(ui::FontRole::Body, dpi);
        captionFont = createUiFont(ui::FontRole::Caption, dpi);
        headingFont = createUiFont(ui::FontRole::Heading, dpi);
        valueFont = createUiFont(ui::FontRole::Value, dpi);
        smallValueFont = createUiFont(ui::FontRole::SmallValue, dpi);
        monitorHeaderFont = createUiFont(ui::FontRole::ClassicBody, dpi);
        monitorCaptionFont = createUiFont(ui::FontRole::MonitorCaption, dpi);
        fanTitleFont = createUiFont(ui::FontRole::FanTitle, dpi);
        fanMetricFont = createUiFont(ui::FontRole::FanMetric, dpi);
        fanSecondaryFont = createUiFont(ui::FontRole::FanSecondary, dpi);
        fanSecondaryCompactFont = createUiFont(ui::FontRole::FanSecondaryCompact, dpi);
        for (HWND control : {pageButtons[0], pageButtons[1], pageButtons[2], pageButtons[3]})
            if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
        SendMessageW(linkCurves, WM_SETFONT, reinterpret_cast<WPARAM>(captionFont), TRUE);
        SendMessageW(ecManualButton, WM_SETFONT, reinterpret_cast<WPARAM>(captionFont), TRUE);
        SendMessageW(ecManualGpuButton, WM_SETFONT, reinterpret_cast<WPARAM>(captionFont), TRUE);
        for (int channel = 0; channel < 2; ++channel) {
            for (HWND button : presetButtons[channel])
                if (button) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(captionFont), TRUE);
            for (int column = 0; column < 8; ++column) {
                for (HWND control : {
                         temperatureEditors[channel][column].edit,
                         temperatureEditors[channel][column].spin,
                         dutyEditors[channel][column].edit,
                         dutyEditors[channel][column].spin}) {
                    if (control) SendMessageW(control, WM_SETFONT,
                                              reinterpret_cast<WPARAM>(captionFont), TRUE);
                }
            }
        }
        HDC labelDc=GetDC(owner);const auto previousLabelFont=SelectObject(labelDc,captionFont);
        SIZE temperatureSize{},pwmSize{};
        ::mc::gui::localizedTextExtent(labelDc,L"Temp °C",7,&temperatureSize);
        ::mc::gui::localizedTextExtent(labelDc,L"PWM %",5,&pwmSize);
        curveLabelWidth=std::max(temperatureSize.cx,pwmSize.cx);
        SelectObject(labelDc,previousLabelFont);ReleaseDC(owner,labelDc);
    }

    // КОНТРАКТ КАРТОЧЕК И ПРОКРУТКИ FANs (16.10.2026).
    // Сохранить существующий рабочий скролл: view занимает полную ширину вкладки,
    // fansGeometry получает viewWidth без дополнительного вычитания scrollLane().
    // contentExtent охватывает все карточки, редакторы и нижнее примечание.
    // Координаты геометрии — координаты контента; HWND-контролы смещаются на
    // -scroll.position() ровно один раз. График, подпись и контролы не расходятся.
    // scrollChanged использует SW_SCROLLCHILDREN и отдельно инвалидирует правую
    // полосу после копирования пикселей. Thumb неподвижен относительно viewport,
    // а не контента; не прокручивать его вместе с карточками и не автоскрывать.
    // При изменении DPI обновлять также шрифты скрытой вкладки, до раннего return.
    // Не переносить сюда другой механизм скролла ради правки SETs/KEYs. Изменение
    // layout/paint/scroll не должно вызывать FAN-режимы или аппаратные записи.
    void arrange(const RECT& area, UINT newDpi, bool show) {
        const UINT targetDpi = newDpi != 0 ? newDpi : 96;
        // A hidden page receives the window DPI too. Update its fonts before
        // returning, so opening SETs first cannot leave FANs at 96 DPI.
        if (dpi != targetDpi || fontLanguage != ui::languageIndex()) {
            dpi = targetDpi;
            createFonts();
            if (telemetryLog) model.traceView(std::format("FANs fonts updated: dpi={} metric_height_px={} caption_height_px={}",
                dpi, ui::fontHeight(ui::FontRole::FanMetric, dpi), ui::fontHeight(ui::FontRole::Caption, dpi)));
        }
        model.setVisible(show);
        if (visible || anyLoggingEnabled()) SetTimer(page, kSensorTimer, kPollIntervalMs, nullptr);
        if (!visible) {
            ShowWindow(page, SW_HIDE);
            if (!anyLoggingEnabled() && IsWindow(page)) KillTimer(page, kSensorTimer);
            return;
        }
        width = std::max(0L, area.right - area.left);
        height = std::max(0L, area.bottom - area.top);
        page::place(page, area.left, area.top, width, height, true);
        layout();
        if (pollCount == 0) model.pollSensors();
        SetTimer(page, kSensorTimer, kPollIntervalMs, nullptr);
        InvalidateRect(view, nullptr, FALSE);
        InvalidateRect(page, nullptr, FALSE);
        page::present(page);
    }

    void layout() {
        if (!visible || width < scale(300) || height < scale(140)) return;
        viewWidth = std::max(1, width);
        page::place(view, 0, 0, viewWidth, height);
        scroll.setViewport(height);
        const auto geometry = ui::fansGeometry(viewWidth, {static_cast<int>(dpi)}, requiredFanHeight(viewWidth),ui::monitorBatteryVisible(deviceProfile(ui::presentationBoard()),monitoring));
        for (int i = 0; i < 2; ++i) {
            fanCards[i] = toNativeRect(geometry.fans[i]);
            graphCards[i] = toNativeRect(geometry.graphs[i]);
            editorCards[i] = toNativeRect(geometry.editors[i]);
            curveCards[i] = toNativeRect(geometry.curves[i]);
        }
        for (int i = 0; i < 5; ++i) monitorCards[i] = toNativeRect(geometry.monitors[i]);
        noteRect = toNativeRect(geometry.note);
        contentExtent = geometry.extent;
        const int oldPosition = scroll.position();
        scroll.setExtent(contentExtent);
        scroll.setPosition(oldPosition);
        measuredWidth = viewWidth;
        positionControls();
    }
    void position(HWND control, RECT rect) {
        if (!control) return;
        OffsetRect(&rect, 0, -scroll.position());
        page::place(control, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    }

    void positionControls() {
        if (measuredWidth <= 0) return;
        for (int channel = 0; channel < 2; ++channel) {
            const auto geometry = ui::editorGeometry(fromNativeRect(editorCards[channel]),
                                                      {static_cast<int>(dpi)}, true,curveLabelWidth);
            auto buttons = geometry.presets;
            int buttonLeft = buttons[0].right + scale(4);
            {
                auto manualRect = buttons[0];
                manualRect.left = buttonLeft; manualRect.right = buttonLeft + scale(52);
                position(channel == 0 ? ecManualButton : ecManualGpuButton, toNativeRect(manualRect));
                buttonLeft = manualRect.right + scale(4);
            }
            buttons[3].left = buttonLeft; buttons[3].right = buttonLeft + scale(40); // Max
            buttonLeft = buttons[3].right + scale(16); // Separate online controls.
            buttons[1].left = buttonLeft; buttons[1].right = buttonLeft + scale(52);
            buttonLeft = buttons[1].right + scale(4);
            buttons[2].left = buttonLeft; buttons[2].right = buttonLeft + scale(40);
            if (channel == 1) {
                auto linkRect = geometry.linkCurves;
                linkRect.left = buttons[1].right + scale(16);
                linkRect.right = linkRect.left + scale(76);
                position(linkCurves, toNativeRect(linkRect));
            }
            auto& selectedPage=channel==0?cpuCurvePage:gpuCurvePage;selectedPage=std::clamp(selectedPage,0,(displayedPointCount(channel)-1)/8);
            const int visiblePage=selectedPage;
            for (int column = 0; column < 8; ++column) {
                placeNumericFieldWindows(temperatureEditors[channel][column],geometry.temperatures[column],geometry.temperatureArrows[column].width(),-scroll.position());
                placeNumericFieldWindows(dutyEditors[channel][column],geometry.duties[column],geometry.dutyArrows[column].width(),-scroll.position());
                const bool active=visiblePage*8+column<displayedPointCount(channel);
                for(auto control:{temperatureEditors[channel][column].edit,temperatureEditors[channel][column].spin,dutyEditors[channel][column].edit,dutyEditors[channel][column].spin})ShowWindow(control,active?SW_SHOW:SW_HIDE);
            }
            const int previousId = channel == 0 ? 0 : 2;
            position(pageButtons[previousId], toNativeRect(geometry.previous));
            position(pageButtons[previousId + 1], toNativeRect(geometry.next));
            const int pageNumber = visiblePage;
            SetWindowTextW(pageButtons[previousId],
                           pageNumber == 0 ? L"‹" : L"‹");
            SetWindowTextW(pageButtons[previousId + 1],
                           pageNumber == 0 ? L"›" : L"›");
            if(nativeCurves){buttons=geometry.presets;ShowWindow(channel==0?ecManualButton:ecManualGpuButton,SW_HIDE);}
            for (int preset = 0; preset < kPresetCount; ++preset) {
                if (!nativeCurves&&mc::kFanCurvePresetOrder[preset] == mc::FanCurvePreset::Min) {
                    ShowWindow(presetButtons[channel][preset], SW_HIDE);
                    continue;
                }
                position(presetButtons[channel][preset], toNativeRect(buttons[preset]));
                InvalidateRect(presetButtons[channel][preset], nullptr, FALSE);
            }
            if(huawei){
                // Preserve shared card/scroll geometry. This board exposes
                // fixed runtime PWM, not the MECHREVO16-point EC table.
                for(int column=0;column<8;++column){
                    // Owner01.11: all native bands stay visible in every mode.
                    // The final immutable MAX row remains read-only, never a draft edit.
                    const bool shown=nativeCurves&&visiblePage*8+column<displayedPointCount(channel);
                    ShowWindow(temperatureEditors[channel][column].edit,shown?SW_SHOW:SW_HIDE);ShowWindow(temperatureEditors[channel][column].spin,shown?SW_SHOW:SW_HIDE);
                    ShowWindow(dutyEditors[channel][column].edit,shown||(!nativeCurves&&!column)?SW_SHOW:SW_HIDE);ShowWindow(dutyEditors[channel][column].spin,shown||(!nativeCurves&&!column)?SW_SHOW:SW_HIDE);
                }
                if(!nativeCurves)placeNumericFieldWindows(dutyEditors[channel][0],geometry.temperatures[0],geometry.temperatureArrows[0].width(),-scroll.position());
                ShowWindow(pageButtons[previousId],nativeCurves&&displayedPointCount(channel)>8?SW_SHOW:SW_HIDE);ShowWindow(pageButtons[previousId+1],nativeCurves&&displayedPointCount(channel)>8?SW_SHOW:SW_HIDE);
                for(int preset=0;preset<kPresetCount;++preset){const auto mode=mc::kFanCurvePresetOrder[preset];ShowWindow(presetButtons[channel][preset],nativeCurves||mode==mc::FanCurvePreset::Default||mode==mc::FanCurvePreset::Max?SW_SHOW:SW_HIDE);}
                ShowWindow(linkCurves,hlylState.sharedCurve?SW_SHOW:SW_HIDE);
            }
        }

        SetWindowTextW(linkCurves, profiles.linked ? L"CPU=GPU" : L"CPU≠GPU");
        if(nativeCurves){updateEditorText(0);updateEditorText(1);}
        updateAvailability();
        InvalidateRect(linkCurves, nullptr, FALSE);
    }
    void updateAvailability() {
        model.syncCustomCurve();
        const bool custom=gm6Controller?gm6State.nativeProfile==mc::gm6::NativeProfile::Custom&&!gm6State.economyActive&&!gm6State.autoPowerActive:
            !preview&&(!model.hasFanController()||!model.fanObservedDefault());
        const auto caption=wide(mc::customManualCaption(deviceProfile(ui::presentationBoard()).id,!preview&&custom,!gm6Controller));
        if(nativeCurves)for(int channel=0;channel<2;++channel)SetWindowTextW(presetButtons[channel][1],hlylState.mode==1?caption.c_str():L"Manual");
        for(auto button:{ecManualButton,ecManualGpuButton})if(button){wchar_t current[32]{};GetWindowTextW(button,current,32);if(caption!=current)SetWindowTextW(button,caption.c_str());}
        const bool ready = !busy && !mc::deviceOperations().interactiveBusy();
        const bool needsRestore = model.fanNeedsRestore();
        const auto enable = [](HWND control, bool value) { if ((IsWindowEnabled(control) != FALSE) != value) EnableWindow(control, value); };
        if(preview){for(int ch=0;ch<2;++ch){const bool edit=profiles.ecManual||profiles.currentPreset(ch)==FanCurvePreset::Manual;
                for(int col=0;col<8;++col)for(auto h:{temperatureEditors[ch][col].edit,temperatureEditors[ch][col].spin,dutyEditors[ch][col].edit,dutyEditors[ch][col].spin})enable(h,edit);
                for(auto h:presetButtons[ch])enable(h,true);}
            for(auto h:{ecManualButton,ecManualGpuButton,linkCurves})enable(h,true);return;}
        if(gm6Controller){const bool available=ready&&gm6State.connected&&gm6State.writable;
            for(int ch=0;ch<2;++ch){
                for(int col=0;col<8;++col){const bool temp=available&&gm6DraftKnown&&!gm6State.needsRestore;
                    enable(temperatureEditors[ch][col].edit,temp);enable(temperatureEditors[ch][col].spin,temp);
                    enable(dutyEditors[ch][col].edit,available&&gm6DraftKnown&&!gm6State.needsRestore);enable(dutyEditors[ch][col].spin,available&&gm6DraftKnown&&!gm6State.needsRestore);}
                for(int p=0;p<kPresetCount;++p){const auto preset=mc::kFanCurvePresetOrder[p];const bool temperatures=monitoring.processors[0].celsius&&monitoring.processors[1].celsius;
                    enable(presetButtons[ch][p],available&&(preset==FanCurvePreset::Default||(!gm6State.needsRestore&&(preset==FanCurvePreset::Max||(preset==FanCurvePreset::Manual&&gm6DraftKnown&&temperatures)))));}}
            enable(ecManualButton,available&&gm6DraftKnown&&!gm6State.needsRestore);enable(ecManualGpuButton,available&&gm6DraftKnown&&!gm6State.needsRestore);enable(linkCurves,available&&gm6State.online&&!gm6State.needsRestore);return;
        }
        if(huawei){
            const bool available=ready&&hlylKnown&&hlylState.writable;
            if(nativeCurves){
                const bool edit=available&&hlylState.mode==1&&hlylState.curves&&!hlylState.needsRestore;
                for(int ch=0;ch<2;++ch)for(int col=0;col<8;++col){const int index=(ch==0?cpuCurvePage:gpuCurvePage)*8+col;const bool ordinary=edit&&index<(hlylState.editablePointCounts[ch]?hlylState.editablePointCounts[ch]:profiles.activePointCount(ch)-1);
                    enable(temperatureEditors[ch][col].edit,ordinary);enable(temperatureEditors[ch][col].spin,ordinary);enable(dutyEditors[ch][col].edit,ordinary);enable(dutyEditors[ch][col].spin,ordinary);}
                for(int ch=0;ch<2;++ch)for(int p=0;p<kPresetCount;++p){const auto mode=mc::kFanCurvePresetOrder[p];enable(presetButtons[ch][p],available&&(mode==mc::FanCurvePreset::Default||!hlylState.needsRestore));}
                enable(ecManualButton,false);enable(ecManualGpuButton,false);enable(linkCurves,false);return;
            }
            for(int channel=0;channel<2;++channel){enable(dutyEditors[channel][0].edit,available&&!hlylState.needsRestore);enable(dutyEditors[channel][0].spin,available&&!hlylState.needsRestore);
                for(int preset=0;preset<kPresetCount;++preset){const auto mode=mc::kFanCurvePresetOrder[preset];enable(presetButtons[channel][preset],available&&(mode==mc::FanCurvePreset::Default||(!hlylState.needsRestore&&(mode==mc::FanCurvePreset::Max||(nativeCurves&&(mode==mc::FanCurvePreset::Manual||mode==mc::FanCurvePreset::Min))))));}}
            enable(ecManualButton,available&&!hlylState.needsRestore);enable(ecManualGpuButton,available&&!hlylState.needsRestore);enable(linkCurves,false);return;
        }
        for (int channel=0;channel<2;++channel) {
            const bool editable = ready && !needsRestore && profiles.currentPreset(channel) != mc::FanCurvePreset::Default;
            for (int column=0;column<8;++column) for (HWND control : {temperatureEditors[channel][column].edit,
                temperatureEditors[channel][column].spin,dutyEditors[channel][column].edit,dutyEditors[channel][column].spin}) enable(control,editable);
            for (int preset=0;preset<kPresetCount;++preset) enable(presetButtons[channel][preset],ready && model.hasFanController() && (!needsRestore || mc::kFanCurvePresetOrder[preset]==mc::FanCurvePreset::Default));
        }
        const bool onlineMode = controlStatus.owned && !controlStatus.ecManual && !controlStatus.maximum &&
            (controlStatus.controlled[0] || controlStatus.controlled[1]);
        enable(linkCurves,ready && !needsRestore && !controlStatus.error && onlineMode);
        enable(ecManualButton,ready && !needsRestore && model.hasFanController());
        enable(ecManualGpuButton,ready && !needsRestore && model.hasFanController());
    }

    int displayedPointCount(int channel)const{
        if(nativeCurves&&hlylKnown&&hlylState.mode!=1&&hlylState.curves&&hlylState.curves->actualPointCounts[channel]>0)
            return std::clamp(hlylState.curves->actualPointCounts[channel],1,16);
        return profiles.activePointCount(channel);
    }
    void updateEditorText(int channel) {
        if(nativeCurves&&!preview){
            if(!hlylKnown||!hlylState.curves)return;
            model.ensureNativeEditorDraft();
            const auto& points=hlylState.mode==1?profiles.curves[0]:hlylState.curves->actual;
            synchronizingEditors=true;
            const int offset=(channel==0?cpuCurvePage:gpuCurvePage)*8;
            for(int col=0;col<8&&offset+col<displayedPointCount(channel);++col){
                const auto& point=points[channel][offset+col];
                SendMessageW(temperatureEditors[channel][col].spin,UDM_SETRANGE32,mc::fanInput::temperatureMinimum,mc::fanInput::temperatureMaximum);
                SendMessageW(dutyEditors[channel][col].spin,UDM_SETRANGE32,mc::fanInput::percentMinimum,mc::fanInput::percentMaximum);
                // Factory/read-only values may lie outside USER input bounds.
                if(GetFocus()!=temperatureEditors[channel][col].edit)SetWindowTextW(temperatureEditors[channel][col].edit,(point.temperature==255?std::wstring{}:std::to_wstring(point.temperature)).c_str());
                if(GetFocus()!=dutyEditors[channel][col].edit)SetWindowTextW(dutyEditors[channel][col].edit,std::to_wstring(point.duty).c_str());
            }
            synchronizingEditors=false;return;
        }
        if(huawei&&!nativeCurves){SendMessageW(dutyEditors[channel][0].spin,UDM_SETPOS32,0,hlylDraft[channel]);return;}
        synchronizingEditors = true;
        const auto curve = model.normalizedEditorCurve(channel);
        const int currentPage = channel == 0 ? cpuCurvePage : gpuCurvePage;
        for (int column = 0; column < 8; ++column) {
            const CurveCell& cell = curve[static_cast<std::size_t>(currentPage * 8 + column)];
            SendMessageW(temperatureEditors[channel][column].spin,UDM_SETRANGE32,mc::fanInput::temperatureMinimum,mc::fanInput::temperatureMaximum);
            if(cell.temperature==255)SetWindowTextW(temperatureEditors[channel][column].edit,L"");
            else SetWindowTextW(temperatureEditors[channel][column].edit,std::to_wstring(cell.temperature).c_str());
            SendMessageW(dutyEditors[channel][column].spin, UDM_SETRANGE32, mc::fanInput::percentMinimum,mc::fanInput::percentMaximum);
            SendMessageW(dutyEditors[channel][column].spin, UDM_SETPOS32, 0,
                         static_cast<LPARAM>(cell.duty));
        }
        synchronizingEditors = false;
    }

    int editValue(HWND edit, int fallback) const {
        wchar_t value[32]{};
        GetWindowTextW(edit, value, static_cast<int>(std::size(value)));
        if (!value[0]) return fallback;
        return _wtoi(value);
    }

    NumericEditor* editorForControl(HWND control) {
        if (!control || GetParent(control) != view) return nullptr;
        const int id = GetDlgCtrlID(control);
        if (id >= kEditBase && id < kEditBase + 32) {
            const int offset = id - kEditBase;
            const int channel = offset / 16;
            const int field = offset % 16;
            const int column = field % 8;
            return field < 8 ? &temperatureEditors[channel][column]
                             : &dutyEditors[channel][column];
        }
        if (id >= kSpinBase && id < kSpinBase + kSpinControlCount) {
            const int offset = id - kSpinBase;
            const int channel = offset / 16;
            const int field = offset % 16;
            const int column = field / 2;
            return field % 2 == 0 ? &temperatureEditors[channel][column]
                                  : &dutyEditors[channel][column];
        }
        return nullptr;
    }

    void handleControlWheel(HWND control, WPARAM wParam, LPARAM lParam) {
        NumericEditor* editor = editorForControl(control);
        POINT cursor{};
        const HWND underCursor = GetCursorPos(&cursor) ? WindowFromPoint(cursor) : nullptr;
        const HWND focus = GetFocus();
        const bool focused = editor && (focus == editor->edit || focus == editor->spin);
        const bool hovered = editor && (underCursor == editor->edit || underCursor == editor->spin);
        if (focused && hovered) {
            if (wheelEditor != editor->edit) {
                wheelEditor = editor->edit;
                wheelRemainder = 0;
            }
            wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
            const int steps = wheelRemainder / WHEEL_DELTA;
            wheelRemainder -= steps * WHEEL_DELTA;
            if (steps != 0) {
                const int field = (GetDlgCtrlID(editor->edit) - kEditBase) % 16;
                const bool temperature = field < 8;
                const int minimum=temperature?mc::fanInput::temperatureMinimum:mc::fanInput::percentMinimum;
                const int maximum=temperature?mc::fanInput::temperatureMaximum:mc::fanInput::percentMaximum;
                const int current = std::clamp(editValue(editor->edit, maximum), minimum, maximum);
                const int next = std::clamp(current + steps, minimum, maximum);
                if (next != current) {
                    SendMessageW(editor->spin, UDM_SETPOS32, 0, static_cast<LPARAM>(next));
                    commitEditors((GetDlgCtrlID(editor->edit) - kEditBase) / 16);
                    if(!huawei){model.markDraftEdited();SetTimer(page, kProfileSaveTimer, 350, nullptr);}
                    InvalidateRect(view, nullptr, FALSE);
                }
            }
            return;
        }

        wheelEditor = nullptr;
        wheelRemainder = 0;
        SendMessageW(view, WM_MOUSEWHEEL, wParam, lParam);
    }

    void commitEditors(int editedChannel = -1) {
        if (synchronizingEditors) return;
        if(nativeCurves&&!preview){
            if(hlylState.mode!=1)return;
            for(int ch=0;ch<2;++ch)if(editedChannel<0||editedChannel==ch){const int offset=(ch==0?cpuCurvePage:gpuCurvePage)*8;
                const int count=hlylState.editablePointCounts[ch]?hlylState.editablePointCounts[ch]:profiles.activePointCount(ch)-1;
                for(int col=0;col<8&&offset+col<count;++col){const int i=offset+col;const auto& p=(profiles.curves[0])[ch][i];
                    wchar_t text[16]{};GetWindowTextW(temperatureEditors[ch][col].edit,text,16);if(!text[0]&&p.temperature==255)continue;
                    model.editNativeBand(ch,i,editValue(temperatureEditors[ch][col].edit,p.temperature),editValue(dutyEditors[ch][col].edit,p.duty));}}

            return;
        }
        if(huawei&&!nativeCurves){for(int channel=0;channel<2;++channel)model.editFixedTarget(channel,editValue(dutyEditors[channel][0].edit,hlylDraft[channel]));return;}
        const int first = editedChannel >= 0 ? editedChannel : 0;
        const int last = editedChannel >= 0 ? editedChannel + 1 : profiles.linked ? 1 : 2;
        for (int channel = first; channel < last; ++channel) {
            if (profiles.currentPreset(channel) == mc::FanCurvePreset::Default) continue;
            auto curve = profiles.current(channel);
            mc::fanInput::normalizeDraft(curve);
            const int currentPage = channel == 0 ? cpuCurvePage : gpuCurvePage;
            for (int column = 0; column < 8; ++column) {
                CurveCell& cell = curve[static_cast<std::size_t>(currentPage * 8 + column)];
                wchar_t entered[32]{};GetWindowTextW(temperatureEditors[channel][column].edit,entered,32);
                cell.temperature = *entered?mc::fanInput::temperature(editValue(temperatureEditors[channel][column].edit,mc::fanInput::temperatureMinimum)):255;
                cell.duty = mc::fanInput::percent(editValue(dutyEditors[channel][column].edit, 100));
                if (profiles.ecManual) {
                    cell.duty = mc::fanInput::percent(cell.duty);
                    synchronizingEditors = true;
                    SendMessageW(dutyEditors[channel][column].spin, UDM_SETPOS32, 0, cell.duty);
                    synchronizingEditors = false;
                }
            }
            const auto previousPreset = profiles.currentPreset(channel);
            model.editCurveDraft(channel, curve);
            if (profiles.linked) updateEditorText(1 - channel);
            if (profiles.currentPreset(channel) != previousPreset) positionControls();
        }
    }











    void changePage(int channel, int nextPage) {
        if (nextPage < 0 || nextPage > (displayedPointCount(channel)-1)/8) return;
        commitEditors();
        model.saveLocalCurveEdits();
        if (channel == 0) cpuCurvePage = nextPage;
        else gpuCurvePage = nextPage;
        updateEditorText(channel);
        positionControls();
        InvalidateRect(view, nullptr, FALSE);
    }















    void notifyOwner() {
        ui::footerStatuses().publish("fans",mc::windows::encoding::utf8(operationStatus));
        if (owner) PostMessageW(owner, WM_APP + 52, 0, 0);
    }

    std::wstring statusText() const {
        if(preview)return L"Просмотр профиля · аппаратное управление отключено";
        if(gm6Controller)return operationStatus.empty()?wide(gm6State.status):operationStatus;
        if(huawei)return operationStatus;
        if (!ec) return L"EC · драйвер недоступен";
        if (pollCount == 0) return L"Опрос EC ещё не выполнен";
        std::wstring consumption = L"—";
        const ResourcePresentationView resources(resourceState);
        if(const auto watts=ui::displayedSystemConsumption(monitoring,&resources)){
            consumption = wide(ui::displayNumber(*watts,1));
            if(monitoring.systemPowerStale||(monitoring.battery.flow==BatteryFlow::Charging&&monitoring.battery.dynamicStale))consumption=L"≈"+consumption;
        }
        return operationStatus + L" · Потребление: " +
               consumption + L" Вт";
    }

    RECT thumb() const {
        RECT client{}; GetClientRect(view, &client);
        return page::scrollThumb(client.right, client.bottom, scroll, dpi, hot, dragging);
    }

    void drawThumb(HDC dc) const {
        page::drawThumb(dc, thumb(), hot, dragging);
    }

    void scrollChanged(int previousPosition) {
        const int delta = previousPosition - scroll.position();
        if (delta != 0) {
            scrollDistancePixels += static_cast<std::uint64_t>(std::abs(delta));
            ScrollWindowEx(view, 0, delta, nullptr, nullptr, nullptr, nullptr,
                           SW_INVALIDATE | SW_SCROLLCHILDREN);
        }
        RECT client{};
        GetClientRect(view, &client);
        client.left = std::max<LONG>(0L, client.right - scrollLane());
        InvalidateRect(view, &client, FALSE);
    }

    void wheel(int delta) {
        noteScrollInput();
        const int previous = scroll.position();
        if (page::wheel(scroll, delta, dpi)) scrollChanged(previous);
    }

    void noteScrollInput() {
        const auto now = std::chrono::steady_clock::now();
        if (scrollInputEvents == 0) scrollBurstStart = now;
        ++scrollInputEvents;
        lastScrollInput = now;
        SetTimer(page, kScrollMetricTimer, 100, nullptr);
    }

    void noteScrollPaint(std::chrono::steady_clock::time_point started) {
        const auto now = std::chrono::steady_clock::now();
        if (scrollInputEvents == 0 || now - lastScrollInput > std::chrono::milliseconds(250)) return;
        const double elapsed = std::chrono::duration<double, std::milli>(now - started).count();
        ++scrollPaintEvents;
        scrollPaintTotalMs += elapsed;
        scrollPaintMaxMs = std::max(scrollPaintMaxMs, elapsed);
    }

    void finishScrollMetrics() {
        if (!scrollInputEvents) {
            KillTimer(page, kScrollMetricTimer);
            return;
        }
        if (std::chrono::steady_clock::now() - lastScrollInput < std::chrono::milliseconds(250)) return;

        const double spanMs = std::chrono::duration<double, std::milli>(
            lastScrollInput - scrollBurstStart).count();
        const double eventHz = spanMs > 0.0
            ? static_cast<double>(scrollInputEvents > 1 ? scrollInputEvents - 1 : 0) * 1000.0 / spanMs
            : 0.0;
        const double paintFps = spanMs > 0.0
            ? static_cast<double>(scrollPaintEvents) * 1000.0 / spanMs
            : 0.0;
        try {
            if (loggingEnabled()) {
                logMessage("ui_metrics", std::format(
                    "FANs scroll: input_events={} span_ms={:.1f} event_hz={:.1f} viewport_px={} content_px={} range_px={} scroll_px={} paints={} paint_hz={:.1f} paint_avg_ms={:.2f} paint_max_ms={:.2f}\n",
                    scrollInputEvents, spanMs, eventHz, scroll.viewport(), scroll.extent(),
                    scroll.maximumPosition(), scrollDistancePixels, scrollPaintEvents, paintFps,
                    scrollPaintEvents ? scrollPaintTotalMs / scrollPaintEvents : 0.0,
                    scrollPaintMaxMs));
            }
        } catch (...) {}

        scrollInputEvents = scrollPaintEvents = 0;
        scrollPaintTotalMs = scrollPaintMaxMs = 0.0;
        scrollDistancePixels = 0;
        KillTimer(page, kScrollMetricTimer);
    }

    RECT shifted(RECT rect) const {
        OffsetRect(&rect, 0, -scroll.position());
        return rect;
    }

    ui::ProcessorCard processorText(int channel) const {
        const ResourcePresentationView resources(resourceState);
        const auto fan=ui::monitoringFanReading(monitoring,channel,&resources);
        return ui::processorCard(monitoring, fan, channel,
            ec ? "UWACPIDriver.sys · EC" : monitoring.processors[channel].fanSource.empty()?"Источник оборотов недоступен":monitoring.processors[channel].fanSource,
            preview ? std::string{} : gpuDisplayDevice(view),&resources);
    }
    int measureTextWidth(HDC dc, const std::string& value, HFONT font) const {
        const auto text = mc::windows::encoding::wide(value); SIZE size{};
        const auto previous = SelectObject(dc, font);
        ::mc::gui::localizedTextExtent(dc, text.c_str(), static_cast<int>(text.size()), &size);
        SelectObject(dc, previous); return static_cast<int>(size.cx);
    }
    ui::TextMetrics fontMetrics(HDC dc, HFONT font) const {
        const auto previous=SelectObject(dc,font); TEXTMETRICW metrics{};
        GetTextMetricsW(dc,&metrics);SelectObject(dc,previous);
        return {static_cast<int>(metrics.tmHeight),static_cast<int>(metrics.tmAscent)};
    }
    int wrappedHeight(HDC dc, const std::string& value, HFONT font, int availableWidth) const {
        RECT rect{0,0,std::max(scale(20),availableWidth),0};
        const auto text = mc::windows::encoding::wide(value); const auto previous = SelectObject(dc,font);
        ::mc::gui::localizedDrawText(dc,text.c_str(),static_cast<int>(text.size()),&rect,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        SelectObject(dc,previous); return std::max(scale(14),static_cast<int>(rect.bottom));
    }
    ui::MetricLineLayout summaryLayout(HDC dc, const ui::ProcessorCard& card, ui::Rect bounds) const {
        return ui::metricLineLayout(card.summary,bounds,fontMetrics(dc,monitorCaptionFont).height,
            [&](const std::string& value) { return measureTextWidth(dc,value,monitorCaptionFont); });
    }
    std::array<int,3> informationHeights(HDC dc, const ui::ProcessorCard& card, int cardWidth) const {
        const auto g = ui::fanReadoutGeometry({0,0,cardWidth,scale(kFanCardHeight)}, {static_cast<int>(dpi)});
        std::array<int,3> heights{};
        heights[0]=summaryLayout(dc,card,{g.information.left+scale(8),0,g.information.right-scale(8),0}).height;
        for (int i=1;i<3;++i) heights[i]=wrappedHeight(dc,card.information[i],monitorCaptionFont,
            g.information.width()-scale(16));
        return heights;
    }
    int requiredFanHeight(int pageWidth) const {
        const auto base=ui::fansGeometry(pageWidth,{static_cast<int>(dpi)});
        HDC dc=GetDC(view); if(!dc)return scale(kFanCardHeight);
        int cardHeight=scale(kFanCardHeight);
        for(int channel=0;channel<2;++channel) {
            const auto card=processorText(channel);
            const HFONT secondaryFont=channel==0?fanSecondaryCompactFont:fanSecondaryFont;
            const auto g=ui::fanReadoutGeometry({0,0,base.fans[channel].width(),scale(kFanCardHeight)},{static_cast<int>(dpi)});
            const int contentWidth=g.secondary.width()-scale(16);
            const int primaryWidth=measureTextWidth(dc,card.secondaryTitle,monitorCaptionFont)+scale(8)+
                measureTextWidth(dc,card.secondaryValue,secondaryFont)+scale(6)+measureTextWidth(dc,card.secondaryUnit,monitorCaptionFont);
            const bool secondaryWrap=primaryWidth>contentWidth;
            cardHeight=std::max(cardHeight,ui::processorCardHeight(informationHeights(dc,card,base.fans[channel].width()),
                wrappedHeight(dc,card.secondarySource,monitorCaptionFont,contentWidth),
                secondaryWrap,{static_cast<int>(dpi)},
                fontMetrics(dc,monitorCaptionFont),fontMetrics(dc,secondaryFont),
                !card.secondaryExtraTitle.empty()));
        }
        ReleaseDC(view,dc);return cardHeight;
    }
    void drawFanCard(HDC dc, RECT bounds, int channel) const {
        bounds=shifted(bounds);
        fill(dc,bounds,theme::kMonitorSurface);
        const auto card=processorText(channel);
        const HFONT secondaryFont=channel==0?fanSecondaryCompactFont:fanSecondaryFont;
        std::array<int,3> valueWidths{}, unitWidths{};
        for (int i=0;i<3;++i) {
            valueWidths[i]=measureTextWidth(dc,card.values[i],fanMetricFont);
            if (i > 0) unitWidths[i]=measureTextWidth(dc,card.valueUnits[i],monitorHeaderFont);
        }
        const auto g=ui::fanReadoutGeometry(fromNativeRect(bounds),{static_cast<int>(dpi)},
            informationHeights(dc,card,bounds.right-bounds.left),
            measureTextWidth(dc,card.secondaryTitle,monitorCaptionFont),measureTextWidth(dc,card.secondaryValue,secondaryFont),
            measureTextWidth(dc,card.secondaryUnit,monitorCaptionFont),valueWidths,unitWidths,
            fontMetrics(dc,monitorCaptionFont),fontMetrics(dc,secondaryFont),
            fontMetrics(dc,fanMetricFont),fontMetrics(dc,monitorHeaderFont),
            measureTextWidth(dc,card.secondaryExtraTitle,monitorCaptionFont),
            measureTextWidth(dc,card.secondaryExtraValue,secondaryFont),
            measureTextWidth(dc,card.secondaryExtraUnit,monitorCaptionFont));
        const UINT single=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
        const UINT centered=DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
        drawText(dc,mc::windows::encoding::wide(card.title).c_str(),toNativeRect(g.heading),fanTitleFont,theme::kMonitorText,single);
        const wchar_t* units[]{L"об/мин",L"PWM",L"Температура"};
        for(int i=0;i<3;++i) {
            drawText(dc,mc::windows::encoding::wide(card.values[i]).c_str(),toNativeRect(g.values[i]),fanMetricFont,theme::kFanValueText,single);
            drawText(dc,units[i],toNativeRect(g.units[i]),monitorCaptionFont,theme::kMonitorText,centered);
            drawText(dc,mc::windows::encoding::wide(card.sources[i]).c_str(),toNativeRect(g.sources[i]),monitorCaptionFont,theme::kMonitorSource,
                DT_CENTER|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS);
            if (i > 0) drawText(dc,mc::windows::encoding::wide(card.valueUnits[i]).c_str(),toNativeRect(g.valueUnits[i]),monitorHeaderFont,theme::kFanValueText,single);
        }
        fill(dc,toNativeRect(g.information),theme::kFanNestedSurface);
        fill(dc,toNativeRect(g.secondary),theme::kFanNestedSurface);
        for(const auto& part : summaryLayout(dc,card,g.informationLines[0]).parts)
            drawText(dc,mc::windows::encoding::wide(part.text).c_str(),toNativeRect(part.bounds),monitorCaptionFont,
                part.value?theme::kFanValueText:theme::kMonitorText,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS);
        for(int i=1;i<3;++i) drawText(dc,mc::windows::encoding::wide(card.information[i]).c_str(),toNativeRect(g.informationLines[i]),
            monitorCaptionFont,theme::kMonitorSource,DT_LEFT|DT_TOP|DT_WORDBREAK);
        drawText(dc,mc::windows::encoding::wide(card.secondaryTitle).c_str(),toNativeRect(g.secondaryTitle),monitorCaptionFont,theme::kMonitorText,centered);
        drawText(dc,mc::windows::encoding::wide(card.secondaryValue).c_str(),toNativeRect(g.secondaryValue),secondaryFont,theme::kTextPrimary,single);
        drawText(dc,mc::windows::encoding::wide(card.secondaryUnit).c_str(),toNativeRect(g.secondaryUnit),monitorCaptionFont,theme::kTextPrimary,single);
        drawText(dc,mc::windows::encoding::wide(card.secondaryExtraTitle).c_str(),toNativeRect(g.secondaryExtraTitle),monitorCaptionFont,theme::kMonitorText,centered);
        drawText(dc,mc::windows::encoding::wide(card.secondaryExtraValue).c_str(),toNativeRect(g.secondaryExtraValue),secondaryFont,theme::kTextPrimary,single);
        drawText(dc,mc::windows::encoding::wide(card.secondaryExtraUnit).c_str(),toNativeRect(g.secondaryExtraUnit),monitorCaptionFont,theme::kTextPrimary,single);
        drawText(dc,mc::windows::encoding::wide(card.secondarySource).c_str(),toNativeRect(g.secondarySource),monitorCaptionFont,theme::kMonitorSource,
            DT_CENTER|DT_TOP|DT_WORDBREAK);
    }

    void drawMonitorTile(HDC dc, RECT bounds, int index,const MonitoringSnapshot& displaySnapshot,const ResourcePresentationView& resources) const {
        if(bounds.right<=bounds.left||bounds.bottom<=bounds.top)return;
        bounds = shifted(bounds);
        fill(dc, bounds, theme::kMonitorSurface);
        if (index == 0) {
            const auto card = ui::batteryCard(displaySnapshot.battery,&resources);
            std::array<int, 3> labelWidths{}, valueWidths{}, rightWidths{};
            for (int row = 0; row < 3; ++row) {
                labelWidths[row] = measureTextWidth(dc, card.left[row].label, monitorHeaderFont);
                valueWidths[row] = measureTextWidth(dc, card.left[row].value, monitorHeaderFont);
                rightWidths[row] = measureTextWidth(dc, card.right[row], monitorHeaderFont);
            }
            const auto g = ui::batteryCardGeometry(fromNativeRect(bounds), {static_cast<int>(dpi)},
                labelWidths, valueWidths, rightWidths);
            const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
            for (int row = 0; row < 3; ++row) {
                const auto color = row == 1 && card.flow == mc::BatteryFlow::Charging ? theme::kBatteryCharging :
                    row == 1 && card.flow == mc::BatteryFlow::Discharging ? theme::kBatteryDischarging : theme::kMonitorText;
                drawText(dc, mc::windows::encoding::wide(card.left[row].label).c_str(), toNativeRect(g.labels[row]),
                    monitorHeaderFont, color, flags);
                drawText(dc, mc::windows::encoding::wide(card.left[row].value).c_str(), toNativeRect(g.values[row]),
                    monitorHeaderFont, theme::kTextPrimary, flags);
                drawText(dc, mc::windows::encoding::wide(card.right[row]).c_str(), toNativeRect(g.rightValues[row]),
                    monitorHeaderFont, theme::kTextPrimary, flags);
            }
            return;
        }
        const auto card = ui::monitoringCard(displaySnapshot, index,&resources);
        const auto title = mc::windows::encoding::wide(card.title), value = mc::windows::encoding::wide(card.value), unit = mc::windows::encoding::wide(card.unit);
        const auto textWidth = [&](const std::wstring& text) {
            const auto previous = SelectObject(dc, monitorHeaderFont);
            SIZE size{}; ::mc::gui::localizedTextExtent(dc, text.c_str(), static_cast<int>(text.size()), &size);
            SelectObject(dc, previous); return static_cast<int>(size.cx);
        };
        const auto g = ui::monitorGeometry(fromNativeRect(bounds), {static_cast<int>(dpi)},
            textWidth(title), textWidth(value), textWidth(unit));
        const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
        drawText(dc, title.c_str(), toNativeRect(g.title), monitorHeaderFont, theme::kMonitorText, flags);
        drawText(dc, value.c_str(), toNativeRect(g.value), monitorHeaderFont, theme::kTextPrimary, flags);
        drawText(dc, unit.c_str(), toNativeRect(g.unit), monitorHeaderFont, theme::kTextPrimary, flags);
        const auto detailColor = card.flow == mc::BatteryFlow::Charging ? theme::kBatteryCharging :
            card.flow == mc::BatteryFlow::Discharging ? theme::kBatteryDischarging : theme::kMonitorText;
        drawText(dc, mc::windows::encoding::wide(card.detail).c_str(), toNativeRect(g.detail), monitorCaptionFont, detailColor, flags);
        drawText(dc, mc::windows::encoding::wide(card.source).c_str(), toNativeRect(g.source), monitorCaptionFont, theme::kMonitorSource, flags);
    }

    int plotX(const RECT& plot, double temperature) const {
        return plot.left + static_cast<int>(std::lround((std::clamp(temperature, 20.0, 100.0) - 20) * (plot.right - plot.left) / 80));
    }

    int plotY(const RECT& plot, double duty) const {
        return plot.bottom - static_cast<int>(std::clamp(duty, 0., 100.) * (plot.bottom - plot.top) / 100.);
    }

    void drawGraph(HDC dc, RECT bounds, int channel) const {
        const ResourcePresentationView resources(resourceState);
        const bool nativeSelected=!preview&&(nativeCurves?
            (hlylKnown&&!hlylState.manual&&resources.defaultFanCurve(channel)!=nullptr):
            (huawei?(hlylKnown?!hlylState.manual:resources.defaultFanCurve(channel)!=nullptr):
             (!profiles.ecManual&&profiles.currentPreset(channel)==FanCurvePreset::Default)));
        const auto native=ui::defaultFanGraph(resources,channel,nativeSelected);
        bounds = shifted(bounds);
        const auto g = ui::graphGeometry(fromNativeRect(bounds), {static_cast<int>(dpi)},
            measureTextWidth(dc,native.curve&&native.curve->rpm?"RPM":nativeCurves?"RPM, %":"PWM, %",captionFont),fontMetrics(dc,captionFont));
        RECT title = toNativeRect(g.title);
        std::wstring graphTitle=deviceProfile(resourceBoard).id=="minisforum-hpbsd"?(channel==0?L"CPU вентилятор":L"Системный вентилятор"):monitoring.processors[channel].fanSource.starts_with("Huawei")?(channel==0?L"Вентилятор 1":L"Вентилятор 2"):channel == 0 ? L"CPU вентилятор" : L"GPU вентилятор";
        if(native.curve)graphTitle=localized(graphTitle)+L" · Default "+wide(native.curve->profile);
        drawText(dc, graphTitle.c_str(),
                 title, captionFont, theme::kTextMuted, DT_CENTER|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS);
        RECT axisTop = toNativeRect(g.axis);
        drawText(dc, native.curve&&native.curve->rpm?L"RPM":nativeCurves?L"RPM, %":L"PWM, %", axisTop, captionFont, theme::kTextMuted,
                 DT_LEFT | DT_TOP | DT_SINGLELINE);
        drawText(dc, L"°C", toNativeRect(g.temperatureUnit), captionFont, theme::kTextMuted,
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        RECT plot = toNativeRect(g.plot);
        const auto graphY=[&](double value){return plotY(plot,value*100/native.maximum);};
        if (plot.right <= plot.left || plot.bottom <= plot.top) return;
        for (int percent = 0; percent <= 100; percent += 25) {
            const int y = graphY(percent*native.maximum/100);
            drawLine(dc, plot.left, y, plot.right, y, theme::kDivider);
            RECT label{bounds.left + scale(2), y - scale(7), plot.left - scale(4), y + scale(7)};
            const std::wstring value = wide(native.axisLabel(percent));
            drawText(dc, value.c_str(), label, captionFont, theme::kTextMuted,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        for (int temperature = 20; temperature <= 100; temperature += 20) {
            const int x = plotX(plot, temperature);
            drawLine(dc, x, plot.top, x, plot.bottom, theme::kDivider);
            RECT label{x - scale(12), plot.bottom + scale(2), x + scale(12), plot.bottom + scale(15)};
            const std::wstring value = std::to_wstring(temperature);
            drawText(dc, value.c_str(), label, captionFont, theme::kTextMuted,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const auto& curve = profiles.current(channel);
        std::vector<mc::NativeFanGraphPoint> active;
        for (const CurveCell& cell : std::span(curve).first(displayedPointCount(channel)))
            if (cell.temperature != 255) active.push_back({static_cast<double>(cell.temperature),static_cast<double>(cell.duty)});
        if(nativeSelected){active.clear();if(native.curve)active=native.curve->points;}
        if(!nativeSelected&&!preview&&nativeCurves&&!hlylState.sharedCurve){
            active.clear();if(hlylKnown&&hlylState.curves){auto shown=hlylState.curves->actual;auto slopes=hlylState.curves->slopes[channel];
                if(hlylState.mode==1)try{const auto encoded=mc::hpbsd::withValues(static_cast<mc::hpbsd::Byte>(hlylState.curves->selector),mc::hpbsd::curve(profiles.curves[0],static_cast<mc::hpbsd::Byte>(hlylState.curves->selector)));shown=mc::hpbsd::points(encoded);for(int i=0;i<displayedPointCount(channel);++i)slopes[i]=encoded[(channel?44:27)+i];}catch(...){}
                for(int t=20;t<=100;++t)active.push_back({static_cast<double>(t),static_cast<double>(mc::hpbsd::targetPercent(shown,slopes,channel,t))});
            }
        }else if(!preview&&nativeCurves&&hlylState.sharedCurve&&hlylState.manual){
            active.clear();const auto& table=hlylState.mode==1?profiles.curves[0][channel]:hlylState.curves->actual[channel];
            double boundary=20;for(int i=0;i<displayedPointCount(channel);++i){if(table[i].temperature==255)break;
                active.push_back({boundary,static_cast<double>(table[i].duty)});boundary=table[i].temperature;}
        }else if(!preview&&huawei&&!nativeSelected){active.clear();if(hlylKnown&&hlylState.manual)active.push_back({20.,static_cast<double>(hlylState.targetPercent[channel])});}
        const COLORREF curveColor = channel == 0 ? theme::kAccent : theme::kAccentSecondary;
        if (!active.empty()) {
            double previousTemperature = 20;
            double previousDuty = active.front().duty;
            drawLine(dc, plotX(plot, previousTemperature), graphY(previousDuty),
                     plotX(plot, active.front().temperature), graphY(previousDuty), curveColor,
                     std::max(1, scale(2)));
            for (std::size_t index = 1; index < active.size(); ++index) {
                const auto& point = active[index];
                const int x = plotX(plot, point.temperature);
                drawLine(dc, plotX(plot, previousTemperature), graphY(previousDuty),
                         x, graphY(previousDuty), curveColor, std::max(1, scale(2)));
                drawLine(dc, x, graphY(previousDuty), x, graphY(point.duty),
                         curveColor, std::max(1, scale(2)));
                previousTemperature = point.temperature;
                previousDuty = point.duty;
            }
            drawLine(dc, plotX(plot, previousTemperature), graphY(previousDuty),
                     plot.right, graphY(previousDuty), curveColor, std::max(1, scale(2)));
        } else {
            RECT empty{plot.left + scale(8), plot.top + scale(8),
                       plot.right - scale(8), plot.bottom - scale(8)};
            drawText(dc, preview?L"Пример · просмотр":profiles.currentPreset(channel) == mc::FanCurvePreset::Default
                     ? L"Default · управление прошивкой EC" : L"Кривая EC не задана", empty, captionFont, theme::kTextMuted,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const FanReadout& reading = channel == 0 ? cpu : gpu;
        if(nativeSelected&&native.curve&&native.temperature&&native.requested){
            const int x=plotX(plot,*native.temperature),y=graphY(*native.requested),radius=scale(6);
            HPEN pen=CreatePen(PS_SOLID,std::max(1,scale(1)),curveColor);
            const auto oldPen=SelectObject(dc,pen);const auto oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
            Ellipse(dc,x-radius,y-radius,x+radius+1,y+radius+1);
            SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
        }
        const auto nativeTemperature=nativeCurves&&hlylState.curves?hlylState.curves->temperatures[channel]:std::optional<int>{};
        if (nativeSelected?(native.curve&&native.temperature&&native.actual):nativeCurves?(nativeTemperature&&reading.rpmAvailable):(reading.temperatureAvailable&&reading.pwmAvailable)) {
            const int x = plotX(plot, nativeSelected?*native.temperature:nativeCurves?*nativeTemperature:reading.temperatureCelsius);
            const int y = graphY(nativeSelected?*native.actual:nativeCurves?(hlylState.sharedCurve?hlylState.actualPercent[channel]:std::clamp((reading.rpm*100+mc::hpbsd::maximumCode[channel]*50)/(mc::hpbsd::maximumCode[channel]*100),0,100)):static_cast<int>(std::lround(reading.dutyPercent)));
            const int radius = scale(4);
            HBRUSH white = CreateSolidBrush(theme::kTextPrimary);
            HGDIOBJ oldBrush = SelectObject(dc, white);
            HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(white);
        }

        RECT legend = toNativeRect(g.legend);
        if(preview){drawText(dc,L"Пример · просмотр",legend,captionFont,theme::kTextSecondary,DT_LEFT|DT_VCENTER|DT_SINGLELINE);return;}
        if(nativeSelected){
            const auto text=native.curve?native.legend:ui::translate("Default · управляющая температура EC недоступна");
            drawText(dc,wide(text).c_str(),legend,captionFont,theme::kTextSecondary,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            return;
        }
        const bool applied = model.curveApplied(channel);
        const wchar_t* curveState = gm6Controller?(gm6State.connected?
            (gm6State.online?(gm6State.onlineControlled[channel]?L"Manual online":L"Штатная кривая EC"):gm6State.boost?L"Fan Boost EC":gm6State.custom?L"Custom EC · график — черновик":L"Штатное управление EC"):L"Нет подтверждения"):
            huawei?(hlylKnown?(hlylState.manual?(nativeCurves?L"Manual RPM":L"Manual PWM"):L"Default EC"):L"Нет подтверждения"):!modeKnown ? L"Нет подтверждения" : applied ? L"Применена" : L"Не применена";
        if(nativeCurves){
            std::wstring fact=hlylState.mode<0?L"Нет подтверждения":hlylState.mode==0?L"Default EC":hlylState.mode==1?(applied?L"Manual EC · применена":L"Manual EC · черновик"):localized(curveState);
            if(hlylKnown&&hlylState.manual&&hlylState.mode!=1&&!hlylState.targetRamped){const int target=(hlylState.targetPercent[channel]*(channel==0?51:40)+50)/100*100;fact+=L" · Target "+std::to_wstring(target)+L" RPM";}
            if(reading.rpmAvailable)fact+=L" · RPM "+std::to_wstring(reading.rpm);
            drawText(dc,fact.c_str(),legend,captionFont,theme::kTextSecondary,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);return;
        }
        if (reading.temperatureAvailable && reading.pwmAvailable) {
            const auto temperature=ui::displayCompactNumber(reading.temperatureCelsius);
            const auto percent=ui::displayNumber(reading.dutyPercent,0);
            const std::wstring fact = localized(curveState)+L" · "+wide(ui::formatMessage(
                "Факт: {temperature} °C · {percent}%",{{"temperature",temperature},{"percent",percent}}));
            RECT lineSample{legend.left, legend.top + scale(7), legend.left + scale(18),
                            legend.top + scale(9)};
            fill(dc, lineSample, curveColor);
            legend.left += scale(23);
            drawText(dc, fact.c_str(), legend, captionFont, theme::kTextSecondary,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            const std::wstring fact = monitoring.processors[channel].fanSource.starts_with("Huawei")?
                L"Штатное управление Huawei · PWM не предоставлен":std::wstring(curveState) + L" · датчик EC недоступен";
            drawText(dc, fact.c_str(), legend, captionFont,
                     theme::kTextMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }





    void drawEditorCard(HDC dc, RECT bounds, int channel) const {
        bounds = shifted(bounds);
        const auto g = ui::editorGeometry(fromNativeRect(bounds), {static_cast<int>(dpi)}, true,curveLabelWidth);
        if(huawei&&!nativeCurves){
            auto label=toNativeRect(g.temperatureLabel);drawText(dc,nativeCurves?L"RPM %":L"PWM %",label,captionFont,theme::kTextSecondary,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

            auto source=toNativeRect(g.source);source.right=bounds.right-scale(8);
            const ResourcePresentationView resources(resourceState);
            const auto native=ui::defaultFanGraph(resources,channel,!preview&&(hlylKnown?!hlylState.manual:resources.defaultFanCurve(channel)!=nullptr));
            const auto caption=preview?std::wstring(L"Пример · просмотр"):native.curve?wide(native.source):std::wstring(L"Manual · 5–100% · Auto ≥80°C");
            drawText(dc,caption.c_str(),source,captionFont,theme::kTextSecondary,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);return;
        }
        const int pageIndex = channel == 0 ? cpuCurvePage : gpuCurvePage;

        RECT rowLabel = toNativeRect(g.temperatureLabel);
        drawText(dc, nativeCurves?L"До °C":L"Temp °C", rowLabel, captionFont, theme::kTextSecondary,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        rowLabel = toNativeRect(g.dutyLabel);
        drawText(dc, nativeCurves?L"RPM %":L"PWM %", rowLabel, captionFont, theme::kTextSecondary,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        for (int column = 0; column < 8; ++column) {
            if(pageIndex*8+column>=displayedPointCount(channel))break;
            const int index = pageIndex * 8 + column;
            RECT heading = toNativeRect(g.numbers[column]);
            const std::wstring number = std::to_wstring(index + 1);
            drawText(dc, number.c_str(), heading, captionFont, theme::kTextMuted,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        if(!preview&&nativeCurves&&!hlylState.sharedCurve){auto source=toNativeRect(g.source);source.right=bounds.right-scale(8);
            const auto* recipe=hlylState.mode==1?&profiles.curves[0]:hlylState.curves?&hlylState.curves->actual:nullptr;
            const int guard=recipe?std::min((*recipe)[channel][displayedPointCount(channel)-2].temperature,channel?83:93)+(channel?0:1):(channel?83:94);
            const auto text=std::to_wstring(mc::fanInput::temperatureMinimum)+L"–"+std::to_wstring(mc::fanInput::temperatureMaximum)+L" °C · "+std::to_wstring(mc::fanInput::percentMinimum)+L"–"+std::to_wstring(mc::fanInput::percentMaximum)+L"% · Max ≥"+std::to_wstring(guard)+L" °C";
            drawText(dc,text.c_str(),source,captionFont,theme::kTextSecondary,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);return;}
        std::wstring downText = preview?L"Пример · просмотр":L"DownT −3 °C";
        if(!preview&&hlylState.sharedCurve)downText+=L" · CPU=GPU · EC RPM";
        if(preview){}
        else if (!modeKnown) downText += L" · режим не подтверждён";
        else if (controlStatus.ecManual && controlStatus.owned)
            downText += L" · Общая кривая EC";
        else if (controlStatus.maximum || nativeFanBoost)
            downText += L" · EC: Max";
        else if (controlStatus.owned && (controlStatus.controlled[0] || controlStatus.controlled[1]))
            downText += controlStatus.linked ? L" · Общая программная кривая" : L" · Раздельные программные кривые";
        else downText += L" · Штатное управление EC";
        {const ResourcePresentationView resources(resourceState);
         const bool selected=!preview&&!profiles.ecManual&&profiles.currentPreset(channel)==FanCurvePreset::Default;
         const auto native=ui::defaultFanGraph(resources,channel,selected);if(native.curve)downText=wide(native.source);}
        RECT pageLabel = toNativeRect(g.page);
        RECT source = toNativeRect(g.source);
        drawText(dc, downText.c_str(), source, captionFont, theme::kTextSecondary,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const auto pageCaption=std::to_wstring(pageIndex*8+1)+L"–"+std::to_wstring(std::min((pageIndex+1)*8,displayedPointCount(channel)))+L" / "+std::to_wstring(displayedPointCount(channel));
        drawText(dc, pageCaption.c_str(),
                 pageLabel, captionFont, theme::kTextSecondary,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const int count = model.definedCount(channel);
        const std::wstring summaryText = preview?std::to_wstring(count)+L" / "+std::to_wstring(displayedPointCount(channel)):profiles.currentPreset(channel) == mc::FanCurvePreset::Default
            ? (channel == 1 ? L"EC" : L"Штатный EC") : (channel == 1 ? L"" : L"Задано: ") + std::to_wstring(count) + L" / "+std::to_wstring(displayedPointCount(channel));
        RECT summary = toNativeRect(g.count);
        drawText(dc, summaryText.c_str(), summary, captionFont, theme::kTextMuted,
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    void paintView(HWND window) {
        const auto paintStarted = std::chrono::steady_clock::now();
        page::Paint painting(window, buffer);
        if (!painting.valid()) return;
        const HDC dc = painting.dc();
        const auto& client = painting.client();
        fill(dc, client, theme::kBackground);
        drawFanCard(dc, fanCards[0], 0);
        drawFanCard(dc, fanCards[1], 1);

        const ResourcePresentationView resources(resourceState);
        for (int index = 0; index < 5; ++index) drawMonitorTile(dc, monitorCards[index], index,monitoring,resources);
        fill(dc, shifted(curveCards[0]), theme::kSurface);
        fill(dc, shifted(curveCards[1]), theme::kSurface);
        drawGraph(dc, graphCards[0], 0);
        drawGraph(dc, graphCards[1], 1);
        drawEditorCard(dc, editorCards[0], 0);
        drawEditorCard(dc, editorCards[1], 1);


        if (scroll.scrollable()) drawThumb(dc);

        painting.finish();
        noteScrollPaint(paintStarted);
    }

    void drawButton(const DRAWITEMSTRUCT& item) {
        const int id = static_cast<int>(item.CtlID);
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool presetButton = id >= kPresetBase && id < kPresetBase + 2 * kPresetCount;
        // A click highlights its requested mode immediately; otherwise show the core state.
        const bool coreKnown = controlStatus.revision != 0 && !controlStatus.error && !controlStatus.needsRestore;
        const bool autonomous = coreKnown && controlStatus.owned && controlStatus.ecManual;
        const bool maxActive = coreKnown ? controlStatus.maximum : modeKnown && nativeFanBoost;
        bool selected = false;
        if (id == kEcManual || id == kEcManualGpu) selected = autonomous;
        else if (id == kLinkCurves)
            selected = coreKnown && controlStatus.owned && !controlStatus.ecManual && !controlStatus.maximum &&
                (controlStatus.controlled[0] || controlStatus.controlled[1]) && controlStatus.linked;
        else if (presetButton) {
            const int channel = (id - kPresetBase) / kPresetCount;
            const auto preset = mc::kFanCurvePresetOrder[(id - kPresetBase) % kPresetCount];
            const bool online = coreKnown && controlStatus.owned && controlStatus.controlled[channel] && !autonomous && !maxActive;
            const bool native = coreKnown ? !controlStatus.owned || (!controlStatus.controlled[channel] && !maxActive) : modeKnown && nativeDefaultMode;
            selected = preset == mc::FanCurvePreset::Default ? native :
                       preset == mc::FanCurvePreset::Max ? maxActive :
                       preset == mc::FanCurvePreset::Manual ? online : false;
        }
        if (requestedHighlight) {
            const auto& requested = *requestedHighlight;
            if (id == kEcManual || id == kEcManualGpu) selected = requested.ecManual;
            else if (id == kLinkCurves)
                selected = !requested.ecManual && requested.linked &&
                    (requested.currentPreset(0) == mc::FanCurvePreset::Manual ||
                     requested.currentPreset(1) == mc::FanCurvePreset::Manual);
            else if (presetButton) {
                const int channel = (id - kPresetBase) / kPresetCount;
                selected = !requested.ecManual && requested.currentPreset(channel) ==
                    mc::kFanCurvePresetOrder[(id - kPresetBase) % kPresetCount];
            }
        }
        if(gm6Controller){
            const int mode=gm6Requested.value_or(!gm6State.connected||gm6State.needsRestore?-1:gm6State.online?3:gm6State.boost?2:
                gm6State.custom&&gm6State.ramActive&&gm6State.overrideActive?1:
                nativeDefaultMode?0:-1);
            if(id==kEcManual||id==kEcManualGpu)selected=mode==1;
            else if(presetButton){const auto preset=mc::kFanCurvePresetOrder[(id-kPresetBase)%kPresetCount];const int ch=(id-kPresetBase)/kPresetCount;const auto channels=gm6Requested?gm6RequestedChannels:gm6State.onlineControlled;
                selected=preset==FanCurvePreset::Default?(mode==0||(mode==3&&!channels[ch])):preset==FanCurvePreset::Max?mode==2:preset==FanCurvePreset::Manual&&mode==3&&channels[ch];}
            else selected=id==kLinkCurves&&mode==3&&gm6State.onlineLinked;
        }
        if(huawei){
            const int mode=model.nativeRequestedModeOr(!hlylKnown?-1:hlylState.mode>=0?hlylState.mode:!hlylState.manual?0:hlylState.targetPercent==std::array<int,2>{100,100}?2:1);
            if(id==kEcManual||id==kEcManualGpu)selected=mode==1;
            else if(presetButton){const auto preset=mc::kFanCurvePresetOrder[(id-kPresetBase)%kPresetCount];selected=preset==mc::FanCurvePreset::Default?mode==0:preset==mc::FanCurvePreset::Max?mode==2:nativeCurves&&preset==mc::FanCurvePreset::Min?mode==3:nativeCurves&&preset==mc::FanCurvePreset::Manual?mode==1:false;}
            else selected=false;
        }
        if(preview){
            if(id==kEcManual||id==kEcManualGpu)selected=profiles.ecManual;
            else if(id==kLinkCurves)selected=profiles.linked;
            else if(presetButton){const int ch=(id-kPresetBase)/kPresetCount;
                selected=!profiles.ecManual&&profiles.currentPreset(ch)==mc::kFanCurvePresetOrder[(id-kPresetBase)%kPresetCount];}
        }
        const HFONT buttonFont = presetButton || id == kLinkCurves || id == kEcManual || id == kEcManualGpu ? captionFont : bodyFont;
        control::drawButton(item, buttonFont, dpi, selected);
    }

    void command(int id, int notification) {
        if(huawei&&!nativeCurves&&id>=kEditBase&&id<kEditBase+32){if(notification==EN_KILLFOCUS){commitEditors();updateEditorText((id-kEditBase)/16);}return;}
        if (notification == EN_KILLFOCUS && !synchronizingEditors &&
            id >= kEditBase && id < kEditBase + 32) {
            commitEditors((id - kEditBase) / 16);
            updateEditorText((id-kEditBase)/16);
            model.saveLocalCurveEdits();
            InvalidateRect(view, nullptr, FALSE);
            return;
        }
        if (notification == EN_CHANGE) {
            if (!synchronizingEditors && id >= kEditBase && id < kEditBase + 32) {
                model.markDraftEdited();
                // A partially typed PWM (including an empty field) is not a
                // committed value. Validate on focus loss or an explicit action.
                if (GetFocus() == GetDlgItem(view, id)) {
                    KillTimer(page, kProfileSaveTimer);
                    return;
                }
                commitEditors((id - kEditBase) / 16);
                SetTimer(page, kProfileSaveTimer, 350, nullptr);
                InvalidateRect(view, nullptr, FALSE);
            }
            return;
        }
        if (notification != BN_CLICKED) return;
        if (id >= kPresetBase && id < kPresetBase + 2 * kPresetCount) {
            model.selectPreset((id - kPresetBase) / kPresetCount, mc::kFanCurvePresetOrder[(id - kPresetBase) % kPresetCount]);
            return;
        }
        switch (id) {
        case kEcManual:
        case kEcManualGpu: model.selectEcManual(); break;
        case kCpuPrevious: changePage(0, cpuCurvePage - 1); break;
        case kCpuNext: changePage(0, cpuCurvePage + 1); break;
        case kGpuPrevious: changePage(1, gpuCurvePage - 1); break;
        case kGpuNext: changePage(1, gpuCurvePage + 1); break;
        case kLinkCurves: model.toggleCurveLink(); break;
        default: break;
        }
    }

    static LRESULT CALLBACK controlProcedure(HWND control, UINT message, WPARAM wParam,
                                              LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<Impl*>(data);
        if (message == WM_MOUSEWHEEL) {
            self.handleControlWheel(control, wParam, lParam);
            return 0;
        }
        if (message == WM_KILLFOCUS && self.editorForControl(control)) {
            self.wheelEditor = nullptr;
            self.wheelRemainder = 0;
        }
        return DefSubclassProc(control, message, wParam, lParam);
    }

    static LRESULT CALLBACK viewProcedure(HWND window, UINT message, WPARAM wParam,
                                           LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<Impl*>(data);
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            self.paintView(window);
            return 0;
        case WM_MOUSEWHEEL:
            self.wheelEditor = nullptr;
            self.wheelRemainder = 0;
            self.wheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_VSCROLL: {
            const HWND source = reinterpret_cast<HWND>(lParam);
            const int id = source ? GetDlgCtrlID(source) : 0;
            if (id >= kSpinBase && id < kSpinBase + kSpinControlCount) {
                self.commitEditors((id - kSpinBase) / 16);
                self.model.saveLocalCurveEdits();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(window);
            RECT client{};
            GetClientRect(window, &client);
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (!self.scroll.scrollable() || x < client.right - self.scrollLane()) break;
            const RECT thumb = self.thumb();
            const int oldPosition = self.scroll.position();
            page::scrollPress(window,self.scroll,thumb,y,self.dragging,self.grabOffset);
            if(self.scroll.position()!=oldPosition)self.scrollChanged(oldPosition);
            return 0;
        }
        case WM_LBUTTONUP:
            if (self.dragging) {
                ReleaseCapture();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (self.dragging) {
                self.dragging = false;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE: {
            RECT client{};
            GetClientRect(window, &client);
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (self.dragging) {
                const RECT thumb = self.thumb();
                const int oldPosition = self.scroll.position();
                page::scrollDrag(self.scroll,thumb,client.bottom,self.dpi,y,self.grabOffset);
                self.scrollChanged(oldPosition);
            }
            const bool nextHot = self.scroll.scrollable() &&
                                 x >= client.right - self.scrollLane();
            if (self.hot != nextHot) {
                self.hot = nextHot;
                InvalidateRect(window, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self.hot) {
                self.hot = false;
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_COMMAND:
            self.command(LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_DRAWITEM:
            self.drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            if (const auto brush = numericFieldColors(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)))
                return reinterpret_cast<LRESULT>(brush);
            break;
        }

        case WM_KEYDOWN: {
            int position = self.scroll.position();
            switch (wParam) {
            case VK_UP: position -= self.scale(24); break;
            case VK_DOWN: position += self.scale(24); break;
            case VK_PRIOR: position += self.scroll.pageStep(false); break;
            case VK_NEXT: position += self.scroll.pageStep(true); break;
            case VK_HOME: position = 0; break;
            case VK_END: position = self.scroll.maximumPosition(); break;
            default: return DefSubclassProc(window, message, wParam, lParam);
            }
            const int oldPosition = self.scroll.position();
            self.scroll.setPosition(position);
            self.scrollChanged(oldPosition);
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK pageProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(window, &ps);
            RECT client{};
            GetClientRect(window, &client);
            fill(dc, client, theme::kBackground);
            EndPaint(window, &ps);
            return 0;
        }
        case WM_TIMER:
            if (wParam == kSensorTimer) self->model.pollSensors();
            else if (wParam == kScrollMetricTimer) self->finishScrollMetrics();
            else if (wParam == kProfileSaveTimer) self->model.saveLocalCurveEdits();
            return 0;
        case kSensorsFinished: self->model.completeSensors(); return 0;
        case kWorkerFinished:
            self->model.completeWorker();
            return 0;
        case WM_MOUSEWHEEL:
            self->wheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};

FansPage::FansPage(HWND parent, mc::EcAccess* ec, OsdPopup* osd, std::function<void(std::string)> log,
                   std::shared_ptr<mc::windows::FanController> controller, std::function<mc::MonitoringSnapshot()> monitoringSource)
    :impl_(std::make_unique<Impl>(parent,ec,osd,std::move(log),std::move(controller),std::move(monitoringSource))){}
FansPage::~FansPage() = default;

void FansPage::arrange(const RECT& area, UINT dpi, bool visible) {
    impl_->arrange(area, dpi, visible);
}

bool FansPage::preTranslate(MSG& message) {
    if (!impl_->visible) return false;
    if ((message.message == WM_LBUTTONDOWN || message.message == WM_RBUTTONDOWN) &&
        impl_->editorForControl(GetFocus()) && !impl_->editorForControl(message.hwnd))
        SetFocus(message.hwnd == impl_->page || IsChild(impl_->page, message.hwnd) ? impl_->view : impl_->owner);
    if (message.message == WM_MOUSEWHEEL &&
        (message.hwnd == impl_->page || IsChild(impl_->page, message.hwnd))) {
        const POINT cursor{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
        const HWND hovered = WindowFromPoint(cursor);
        // Handle wheel input before native buddy subclasses can process it.
        // The same focus/hover guard serves both EDIT and UPDOWN controls.
        impl_->handleControlWheel(impl_->editorForControl(hovered) ? hovered : message.hwnd,
                                  message.wParam, message.lParam);
        return true;
    }
    return IsDialogMessageW(impl_->page, &message) != FALSE;
}

std::wstring FansPage::statusText() const {
    return impl_->statusText();
}
void FansPage::refreshCommandAvailability() { if(impl_)impl_->updateAvailability(); }
void FansPage::refreshDeviceState(){
    if(impl_)impl_->model.refreshDeviceState();
}

}  // namespace mc::gui
