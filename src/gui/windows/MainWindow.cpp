#include "MagicControl/ActionLog.h"
#include "PageMechanics.h"
#include "../common/FooterStatus.h"
#include "../common/CpuPowerLabels.h"
#include "../common/KeysPolicy.h"
#include "../common/DpiChangeTrace.h"
#include "LocalizationWindows.h"
#include "MainWindow.h"
#include "MagicControl/KeysLifecycle.h"
#include "InstanceHandoff.h"
#include "MessageDialog.h"
#include "ControlTheme.h"
#include "MagicControl/PawnIoInstall.h"
#include "UpdateWindow.h"
#include "MagicControl/DeviceProfile.h"
#include "MagicControl/LiquidCooling.h"
#include "MagicControl/NvidiaGpu.h"
#include "LanguagePopup.h"
#include "../common/BatteryPolicy.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <stdexcept>
#include <utility>
#include <deque>

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <powrprof.h>

#include "MagicControl/BoardIdentity.h"
#include "MagicControl/EcAccess.h"
#include "MagicControl/Errors.h"
#include "MagicControl/IEcBackend.h"
#include "MagicControl/Logging.h"
#include "MagicControl/DeviceOperation.h"
#include "MagicControl/KeyboardBacklight.h"
#include "../../platform/windows/FanControlWindows.h"
#include "../../devices/mechrevo/gm6ir0c/ControlWindows.h"
#include "../../platform/windows/NativeBatteryWindows.h"
#include "../../platform/windows/ApplicationPreferencesWindows.h"
#include "../../platform/windows/InfoOperationsWindows.h"
#include "MagicControl/BatteryIdentityHistory.h"
#include "MagicControl/Version.h"
#include "../../platform/windows/OemEventMonitor.h"
#include "../../platform/windows/WindowsDisplay.h"
#include "MagicControl/DisplayRotationControl.h"
#include "../../platform/windows/WindowsDevices.h"
#include "../common/OsdMessages.h"
#include "../common/DeviceHeader.h"
#include "../../platform/windows/TextWindows.h"
#include "IconResource.h"

#pragma comment(lib, "dwmapi.lib")

namespace mc::gui {
struct MainWindow::NativeProfileInbox {
    std::mutex gate;
    bool closed{};
    std::deque<std::pair<std::uint64_t,windows::Gm6NativeProfileResult>> results;
};
namespace {

// Existing action logging only; no UI callbacks, polling or hardware access.
// These breadcrumbs are emitted solely while handling a native DPI change.
void logDpiChange(void*,const ui::DpiTraceRecord& row) noexcept {
    try{
        std::string fields="\"trace_id\":"+std::to_string(row.id)+",\"parent_trace_id\":"+std::to_string(row.parent)+
            ",\"stage\":"+audit::quote(ui::dpiTraceStageName(row.stage))+
            ",\"phase\":"+audit::quote(row.phase==ui::DpiTracePhase::Position?"position":"relayout")+
            ",\"old_dpi\":"+std::to_string(row.previousDpi)+",\"new_dpi\":"+std::to_string(row.requestedDpi);
        if(!row.page.empty())fields+=",\"page\":"+audit::quote(row.page)+",\"show_requested\":"+(row.showRequested?"true":"false");
        audit::event("ui.dpi",fields);
    }catch(...){} // Diagnostics never change window handling.
}

constexpr wchar_t kWindowClassName[] = L"MagicControlMainWindow";
constexpr wchar_t kWindowTitle[] = L"MagicControl";

/// Window button ordering in the title bar.
constexpr int kButtonMinimise = 0;
constexpr int kButtonMaximise = 1;
constexpr int kButtonClose = 2;

/// Ordinal values of the system cursor and icon, passed to LoadImageW.
/// The IDC_* / IDI_* macros already expand to a non-const resource pointer,
/// which a Unicode build cannot hand to LoadImageW without a cast that also
/// trips a truncation warning, so the numbers are written out once here.
constexpr WORD kArrowCursorOrdinal = 32512;      // OCR_NORMAL

/// A frame slower than this is likely to be perceived as stutter.
constexpr ULONGLONG kSlowFrameMilliseconds = 12;

/// Minimum gap between two slow-frame log entries.
constexpr ULONGLONG kSlowFrameLogIntervalMilliseconds = 2000;
constexpr UINT kOemEventMessage = WM_APP + 51;
constexpr UINT kFansStatusMessage = WM_APP + 52;
constexpr UINT kSystemStateMessage = WM_APP + 53;
constexpr UINT kLanguageMenuFirst = 2100;
constexpr UINT kTrayMessage = WM_APP + 54;
constexpr UINT kOpenFromTrayMessage = WM_APP + 55;
constexpr UINT kDeviceOperationMessage = WM_APP + 56;
constexpr UINT kScreenFlipFinishedMessage = WM_APP + 57;
constexpr UINT kKeyboardActivityMessage = WM_APP + 58;
constexpr UINT kConfigurationRestoreMessage = WM_APP + 59;
constexpr UINT kPawnIoStartupMessage = WM_APP + 60;
constexpr UINT kNativeProfileFinishedMessage = WM_APP + 61;
constexpr UINT kOverclockReadyMessage = WM_APP + 62;
constexpr UINT_PTR kTrayRefreshTimer = 76;
constexpr UINT kTrayOpen = 2200, kTrayExit = 2201;
constexpr UINT kTrayBattery = 2210, kTrayPower = 2220, kTrayBrightness = 2230;

/// URLs opened by the two links on the BIOS page.
constexpr const wchar_t* kSupportUrl = L"https://www.mechrevo.com/";
constexpr const wchar_t* kNahimicUpdatesUrl = L"https://www.nahimic.com/updates/";

/// Converts a UTF-8 string from the inventory into the UTF-16 the GDI text
/// calls expect.

/// Opens a URL in the default browser.
///
/// ShellExecuteW is used rather than a hard dependency on a browser: the link
/// belongs to the user's environment, not to this application.
bool openExternalUrl(const wchar_t* url) {
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
    // ShellExecuteW returns a value above 32 on success.
    return reinterpret_cast<INT_PTR>(result) > 32;
}

/// True when a point lies inside a rectangle.
bool contains(const RECT& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] LPCWSTR ordinalResource(WORD identifier) {
    return reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(identifier));
}

/// Redraws the whole window after a state change that affects the chrome.
void repaint(HWND window) { InvalidateRect(window, nullptr, FALSE); }

/// Reports the DPI awareness Windows actually granted. If the manifest were
/// ignored this reads as "unaware", and every measurement taken afterwards would
/// be in virtualised pixels.
[[nodiscard]] const char* awarenessName() {
    switch (GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext())) {
        case DPI_AWARENESS_UNAWARE:
            return "unaware";
        case DPI_AWARENESS_SYSTEM_AWARE:
            return "system";
        case DPI_AWARENESS_PER_MONITOR_AWARE:
            return "per-monitor";
        default:
            return "unknown";
    }
}

/// Appends a message to the current rotated application journal.
///
/// The GUI has no console, so placement and DPI facts go to a file. This is how
/// a scaling problem is diagnosed on a machine that cannot be debugged directly.
void appendStartupLog(const std::string& line) {
    logMessage("application", line);
}

}  // namespace

// ---------------------------------------------------------------------------
// Brush cache
// ---------------------------------------------------------------------------

HBRUSH MainWindow::BrushCache::get(ColorRef color) {
    for (std::size_t index = 0; index < count; ++index) {
        if (entries[index].first == color) {
            return entries[index].second;
        }
    }

    HBRUSH brush = CreateSolidBrush(color);
    if (brush == nullptr) {
        return nullptr;
    }
    if (count < entries.size()) {
        entries[count] = {color, brush};
        ++count;
    }
    return brush;
}

void MainWindow::BrushCache::clear() {
    for (std::size_t index = 0; index < count; ++index) {
        if (entries[index].second != nullptr) {
            DeleteObject(entries[index].second);
        }
        entries[index] = {0, nullptr};
    }
    count = 0;
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

MainWindow::MainWindow() = default;
MainWindow::~MainWindow() {
    suspendLiquidCooling(); // Existing session only; no discovery/connect or UI changes.
    deviceOperations().setNotifier({});
    removeTrayIcon();
    if (trayMenuOwner_) DestroyWindow(trayMenuOwner_);
    oemEvents_.reset();
    closeNativeProfileCommands();
    if(screenFlip_)screenFlip_->shutdown();
    systemEvents_.reset();
    powerDiagnostics_.reset();
    if (fanController_) fanController_->setConfigurationRestoreNotifier({});
    configurationRestore_.reset();
    fansPage_.reset();
    realFansPage_.reset();realSettingsPage_.reset();realKeysPage_.reset();
    keysPage_.reset();
    settingsPage_.reset();
    fanController_.reset();
    biosPage_.reset();
    nvidiaPage_.reset();radeonPage_.reset();
    osd_.reset();
    clearFrameBuffer();
    destroyFonts();
    brushes_.clear();
}

void MainWindow::destroyFonts() {
    HFONT* fonts[] = {&fontBrand_, &fontTab_, &fontTabActive_, &fontStatus_};
    for (HFONT* font : fonts) {
        if (*font != nullptr) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
}

bool MainWindow::create(HINSTANCE instance, int showCommand, Tab startTab, bool noEc) {
    instance_ = instance;
    activeTab_ = startTab;
    noEc_ = noEc;
    try {
        const auto bootstrap=windows::loadApplicationPreferencesForStartup(preferences_);
        ui::setLanguage(preferences_.language);selectedLanguage_=static_cast<int>(preferences_.language);
        static_cast<void>(initializeLogging(preferences_.allLogs,{},preferences_.batteryLog));
        if (!bootstrap.applicationError.empty()) appendStartupLog("preferences: " + bootstrap.applicationError);
        if (!bootstrap.keysError.empty()) appendStartupLog("KEYS bootstrap: " + bootstrap.keysError);
        const auto fontDirectory=portableSettingsPath("fonts");
        if(!registerBundledUiFonts(fontDirectory))throw std::runtime_error("Bundled UI fonts are missing or invalid; repair the fonts directory.");
        setEmbeddedUiFontEnabled(true);
        appendStartupLog("font: configured=embedded-only NotoSansSC=loaded NotoSansTC=loaded");
    } catch(const std::exception& error) {
        appendStartupLog(std::string("font/preferences startup: ")+error.what());
        return false;
    }
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    iconLarge_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_MAGICCONTROL), IMAGE_ICON, 32, 32, LR_SHARED));
    iconSmall_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_MAGICCONTROL), IMAGE_ICON, 16, 16, LR_SHARED));
    iconHeader_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_MAGICCONTROL), IMAGE_ICON,
        kHeaderIconSize, kHeaderIconSize, LR_SHARED));

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &MainWindow::windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor =
        static_cast<HCURSOR>(LoadImageW(nullptr, ordinalResource(kArrowCursorOrdinal),
                                        IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    windowClass.hbrBackground = nullptr;  // Every pixel is painted by WM_PAINT.
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hIcon = iconLarge_;
    windowClass.hIconSm = iconSmall_;

    if (RegisterClassExW(&windowClass) == 0) {
        return false;
    }

    // The reference layout is expressed in logical pixels at 96 DPI, so at 200
    // percent scaling it becomes twice as large in physical pixels. On a
    // 2560x1600 panel that does not fit, and centring an oversized window would
    // push its title bar off screen. Clamp to the monitor's work area, which
    // already accounts for the taskbar and for per-monitor DPI.
    const int dpi = GetDpiForSystem();

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const bool haveMonitor =
        GetMonitorInfoW(MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY),
                        &monitor) != 0;

    // Layout constants are logical pixels. Convert them to physical client
    // pixels before creating the per-monitor-DPI-aware window, then clamp only
    // when the work area is genuinely smaller. DeepSeek's previous build used
    // 940x840 physical pixels at 200% DPI, producing the tiny broken window.
    int clientWidth = MulDiv(kDefaultWidth, dpi, 96);
    int clientHeight = MulDiv(kDefaultHeight, dpi, 96);
    if (haveMonitor) {
        const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
        const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
        clientWidth = std::min(clientWidth, workWidth);
        clientHeight = std::min(clientHeight, workHeight);
    }

    RECT desired{0, 0, clientWidth, clientHeight};
    AdjustWindowRectExForDpi(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0, static_cast<UINT>(dpi));

    int windowWidth = desired.right - desired.left;
    int windowHeight = desired.bottom - desired.top;
    int windowLeft = 0;
    int windowTop = 0;

    if (haveMonitor) {
        // Work-area extents as int, because a RECT holds LONGs and std::min
        // cannot deduce a common type between LONG and int.
        const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
        const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
        windowWidth = std::min(windowWidth, workWidth);
        windowHeight = std::min(windowHeight, workHeight);
        windowLeft = monitor.rcWork.left + (workWidth - windowWidth) / 2;
        windowTop = monitor.rcWork.top + (workHeight - windowHeight) / 2;
    }

    std::string log = "build=";
    log += MAGICCONTROL_VERSION_STRING;
    log += " dpi=";
    log += std::to_string(dpi);
    log += " awareness=";
    log += awarenessName();
    log += " screen=";
    log += std::to_string(GetSystemMetrics(SM_CXSCREEN));
    log += "x";
    log += std::to_string(GetSystemMetrics(SM_CYSCREEN));
    log += " work=";
    log += std::to_string(haveMonitor ? monitor.rcWork.right - monitor.rcWork.left : 0);
    log += "x";
    log += std::to_string(haveMonitor ? monitor.rcWork.bottom - monitor.rcWork.top : 0);
    log += " client=";
    log += std::to_string(clientWidth);
    log += "x";
    log += std::to_string(clientHeight);
    log += " window=";
    log += std::to_string(windowWidth);
    log += "x";
    log += std::to_string(windowHeight);
    log += " at=";
    log += std::to_string(windowLeft);
    log += ",";
    log += std::to_string(windowTop);
    appendStartupLog(log);

    window_ = CreateWindowExW(0, kWindowClassName, kWindowTitle,
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, windowLeft, windowTop,
                              windowWidth, windowHeight, nullptr, nullptr, instance, this);

    if (window_ == nullptr) {
        appendStartupLog("CreateWindowExW failed");
        return false;
    }

    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    if (!pawnIoStartupMessage_.empty() && !noEc_) PostMessageW(window_, kPawnIoStartupMessage, 0, 0);
    return true;
}

int MainWindow::runMessageLoop() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (biosPage_ && biosPage_->preTranslate(message)) continue;
        if (radeonPage_ && radeonPage_->preTranslate(message)) continue;
        if (settingsPage_ && settingsPage_->preTranslate(message)) continue;
        if (keysPage_ && keysPage_->preTranslate(message)) continue;
        if (fansPage_ && fansPage_->preTranslate(message)) continue;
        if (nvidiaPage_ && nvidiaPage_->preTranslate(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK MainWindow::windowProcedure(HWND window, UINT message, WPARAM wParam,
                                             LPARAM lParam) {
    MainWindow* self = nullptr;

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Message handling
// ---------------------------------------------------------------------------

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if(const auto request=gracefulInstanceExitMessage();request&&message==request){
        if(wParam==0)return kInstanceExitProtocol;
        if(wParam==1){appendStartupLog("Instance handoff: graceful exit requested by newer local executable");requestExit();return kInstanceExitProtocol;}
        return 0;
    }
    if (taskbarCreated_ && message == taskbarCreated_) {
        trayAdded_ = false;
        if (!addTrayIcon() && hiddenToTray_) openFromTray();
        return 0;
    }
    switch (message) {
        case WM_NCCALCSIZE:
            // Removing the standard non-client area is what makes the custom
            // title bar possible. Returning 0 without adjusting the rectangle
            // keeps the whole window as client area, while DWM still draws the
            // shadow and WM_NCHITTEST still provides resize borders.
            if (wParam == TRUE) {
                return 0;
            }
            break;

        case WM_NCHITTEST: {
            const POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            return onNcHitTest(screenPoint);
        }

        case WM_GETMINMAXINFO:
            onGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lParam));
            return 0;

        case WM_CREATE:
            onCreate();
            applyApplicationIcons();
            addTrayIcon();
            SetTimer(window_, kTrayRefreshTimer, 5000, nullptr);
            return 0;
        case WM_CLOSE:
            if (!exiting_ && trayAdded_) hideToTray();
            else DestroyWindow(window_);
            return 0;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION:
            if (wParam) requestExit();
            return 0;
        case WM_SHOWWINDOW:
            updatePageVisibility(wParam && !hiddenToTray_ && !IsIconic(window_));
            break;
        case WM_TIMER:
            if (wParam == kTrayRefreshTimer) {
                const bool present=nvidiaGpuPresent();
                const bool radeon=radeonDiscretePresent();
                if(present!=nvidiaPresent_||radeon!=radeonPresent_){nvidiaPresent_=present;radeonPresent_=radeon;
                    if(ui::showRadeonTab(radeonPresent_)&&!radeonPage_)radeonPage_=std::make_unique<DiscreteGpuPage>(window_,GpuVendor::Radeon,osd_.get());
                    if(!ui::showRadeonTab(radeonPresent_)&&activeTab_==Tab::Radeon)activeTab_=Tab::Fans;
                    if(present&&!nvidiaPage_)nvidiaPage_=std::make_unique<DiscreteGpuPage>(window_,GpuVendor::Nvidia,osd_.get());
                    if(!ui::showNvidiaTab(present)&&activeTab_==Tab::Nvidia)activeTab_=Tab::Fans;
                    updatePageVisibility(!hiddenToTray_&&!IsIconic(window_));repaint(window_);
                }
                if (!trayAdded_) addTrayIcon();
                if (settingsPage_) settingsPage_->refreshForTray();
                return 0;
            }
            break;
        case WM_POWERBROADCAST:
            if(wParam==PBT_APMSUSPEND&&configurationRestore_)configurationRestore_->suspend();
            // KEYs/INFO are computer-wide services, even without an EC profile.
            if(keysPage_&&!exiting_){
                if(wParam==PBT_APMSUSPEND)keysPage_->suspend();
                else if(wParam==PBT_APMRESUMEAUTOMATIC||wParam==PBT_APMRESUMESUSPEND||wParam==PBT_APMRESUMECRITICAL)keysPage_->restore("power_resume");
            }
            if(wParam==PBT_APMSUSPEND||wParam==PBT_APMRESUMEAUTOMATIC||wParam==PBT_APMRESUMESUSPEND||wParam==PBT_APMRESUMECRITICAL)suspendLiquidCooling();
            if(wParam==PBT_APMSUSPEND||wParam==PBT_APMRESUMEAUTOMATIC||wParam==PBT_APMRESUMESUSPEND||wParam==PBT_APMRESUMECRITICAL)
                invalidateDiagnosticFanSessions();
            if (powerDiagnostics_) powerDiagnostics_->broadcast(wParam, lParam);
            if(!noEc_&&!exiting_){
                if(wParam==PBT_APMSUSPEND){windows::suspendKeyboardForPower();}
                else if(wParam==PBT_APMRESUMEAUTOMATIC||wParam==PBT_APMRESUMESUSPEND||wParam==PBT_APMRESUMECRITICAL){
                    if(windows::resumeKeyboardForPower())restoreConfiguration("power_resume");
                }
            }
            if(!noEc_&&!exiting_&&(wParam==PBT_APMRESUMEAUTOMATIC||wParam==PBT_APMRESUMESUSPEND||wParam==PBT_APMRESUMECRITICAL))
                windows::restoreNativeBatteryForPower("power_resume");
            if (fanController_) {
                if (wParam == PBT_APMSUSPEND) {
                    adapterNoticeAfter_ = ~ULONGLONG{};
                    fanController_->suspendPower();
                }
                else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMRESUMECRITICAL) {
                    adapterNoticeAfter_ = GetTickCount64() + 5000;
                    fanController_->resumePower();
                    restoreConfiguration("power_resume");
                }
                // Battery status updates never check or restore configuration.
                // Adapter feedback is handled separately by GUID_ACDC_POWER_SOURCE.
                else if (wParam == PBT_POWERSETTINGCHANGE && lParam) {
                    const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
                    if (setting->PowerSetting == GUID_SESSION_DISPLAY_STATUS && setting->DataLength == sizeof(DWORD)) {
                        DWORD state{}; std::memcpy(&state, setting->Data, sizeof(state));
                        fanController_->sessionDisplayChanged(state);
                        if (sessionDisplayState_ && *sessionDisplayState_ == 0 && state != 0) {
                            adapterNoticeAfter_ = GetTickCount64() + 5000;
                        }
                        sessionDisplayState_ = state;
                    }
                    if (setting->PowerSetting == GUID_ACDC_POWER_SOURCE && setting->DataLength == sizeof(DWORD)) {
                        DWORD source{}; std::memcpy(&source, setting->Data, sizeof(source));
                        if (settingsPage_) settingsPage_->refreshCommandAvailability();
                        if (acPowerSource_ && *acPowerSource_ != source) {
                            if (source <= 1 && GetTickCount64() >= adapterNoticeAfter_ && configurationRestore_)
                                configurationRestore_->powerSourceChanged(source == 0);
                        }
                        acPowerSource_ = source;
                    }
                }
            }
            if(!exiting_&&(wParam==PBT_APMRESUMEAUTOMATIC||wParam==PBT_APMRESUMESUSPEND||wParam==PBT_APMRESUMECRITICAL)){
                // An in-flight SETs observation from before sleep must not
                // supersede fresh post-wake state. Existing admission and
                // paused-observation checks still control when reads start.
                if(settingsPage_)settingsPage_->refreshForTray(true);
                if(realSettingsPage_)realSettingsPage_->refreshForTray(true);
                restoreKeysApplicationFlags();
            }
            return TRUE;
        case kScreenFlipFinishedMessage: completeScreenFlip(); return 0;
        case kKeyboardActivityMessage: if (fanController_) fanController_->keyboardActivity(); return 0;
        case kConfigurationRestoreMessage:
            adapterNoticeAfter_ = GetTickCount64() + 5000;
            setKeyboardLightingSuspended(true);setKeyboardLightingSuspended(false);
            restoreConfiguration("system_sleep_clock");
            if(settingsPage_)settingsPage_->refreshForTray(true);
            if(realSettingsPage_)realSettingsPage_->refreshForTray(true);
            return 0;
        case kPawnIoStartupMessage:
            if (!pawnIoStartupMessage_.empty()) {
                const auto text = std::exchange(pawnIoStartupMessage_, std::wstring{});
                showInformationDialog(window_, dpi_, text);
            }
            return 0;
        case kOpenFromTrayMessage: openFromTray(); return 0;
        case kTrayMessage: {
            const UINT event = LOWORD(lParam);
            if (event == WM_LBUTTONDBLCLK || event == NIN_KEYSELECT) openFromTray();
            else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
                POINT point{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)};
                if (event == WM_RBUTTONUP || (point.x == -1 && point.y == -1)) GetCursorPos(&point);
                showTrayMenu(point);
            }
            return 0;
        }

        case WM_DPICHANGED: {
            const int newDpi = HIWORD(wParam);
            ui::DpiChangeTrace trace(loggingEnabled()?logDpiChange:nullptr,nullptr,static_cast<int>(dpi_),newDpi);
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            trace.positioned();
            onDpiChanged(newDpi);
            return 0;
        }

        case WM_SIZE:
            width_ = LOWORD(lParam);
            height_ = HIWORD(lParam);
            if (wParam != SIZE_MINIMIZED && !hiddenToTray_) trayRestoreMaximized_ = IsZoomed(window_) != FALSE;
            updatePageVisibility(wParam != SIZE_MINIMIZED && !hiddenToTray_);
            repaint(window_);
            return 0;

        case WM_MOUSEWHEEL:
            if(activeTab_==Tab::OvClock){
                if(page::wheel(overclockScroll_,GET_WHEEL_DELTA_WPARAM(wParam),dpi_)){
                    arrangeOverclock(true);const auto area=contentRect();InvalidateRect(window_,&area,FALSE);
                }
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            if(overclockDragging_){overclockDragging_=false;const auto area=contentRect();InvalidateRect(window_,&area,FALSE);}
            break;

        case WM_ERASEBKGND:
            // WM_PAINT covers every pixel, so erasing first only causes flicker.
            return 1;

        case WM_PAINT:
            onPaint();
            return 0;

        case WM_MEASUREITEM: {
            auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if(item&&item->CtlType==ODT_MENU&&item->itemID>=kLanguageMenuFirst&&item->itemID<kLanguageMenuFirst+ui::kLanguageOptions.size()) {
                item->itemWidth=scale(kLanguageMenuWidth-kLanguageMenuWindowsGutterWidth);
                item->itemHeight=scale(kLanguageMenuRowHeight);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if(item&&item->CtlType==ODT_MENU&&item->itemID>=kLanguageMenuFirst&&item->itemID<kLanguageMenuFirst+ui::kLanguageOptions.size()) {
                paintLanguageItem(*item);return TRUE;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            if(activeTab_==Tab::OvClock&&overclockDragging_){
                const auto area=contentRect();const int w=area.right-area.left,h=area.bottom-area.top;
                const auto thumb=page::scrollThumb(w,h,overclockScroll_,dpi_,true,true);
                page::scrollDrag(overclockScroll_,thumb,h,dpi_,GET_Y_LPARAM(lParam)-area.top,overclockGrabOffset_);
                arrangeOverclock(true);InvalidateRect(window_,&area,FALSE);return 0;
            }
            if (!trackingMouse_) {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = window_;
                if (TrackMouseEvent(&track) != 0) {
                    trackingMouse_ = true;
                }
            }
            onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }

        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            onMouseLeave();
            return 0;

        case WM_LBUTTONDOWN:
            onLeftButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_SETCURSOR: {
            POINT point{};GetCursorPos(&point);ScreenToClient(window_,&point);
            if(PtInRect(&versionRect_,point)){SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32649)));return TRUE;}
            break;
        }

        case WM_LBUTTONUP:
            onLeftButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_NCACTIVATE:
            // The chrome looks the same focused or not, matching the reference.
            return TRUE;

        case kOemEventMessage:
            handleOemEvent(static_cast<std::uint64_t>(wParam), static_cast<std::uint64_t>(lParam));
            return 0;
        case kNativeProfileFinishedMessage:
            finishNativeProfileCommands();return 0;
        case kOverclockReadyMessage:
            if(overclock_&&overclock_->take()){
                const auto value=overclock_->snapshot().memorySwitch;
                if(overclockOff_)SetWindowTextW(overclockOff_,value&&*value==0?L"✓ Выкл. 0":L"Выкл. 0");
                if(overclockOn_)SetWindowTextW(overclockOn_,value&&*value==1?L"✓ Вкл. 1":L"Вкл. 1");
                const auto area=contentRect();InvalidateRect(window_,&area,FALSE);
            }
            return 0;
        case kSystemStateMessage:
            if (keysPage_ && static_cast<mc::SystemEventKind>(wParam)==mc::SystemEventKind::Touchpad && lParam>=0)
                keysPage_->observeState(9,lParam!=0);
            else if (keysPage_ && static_cast<mc::SystemEventKind>(wParam)==mc::SystemEventKind::Camera && lParam>=0)
                keysPage_->observeState(8,lParam!=0);
            else if(keysPage_&&static_cast<mc::SystemEventKind>(wParam)==mc::SystemEventKind::Microphone&&lParam>=0)
                keysPage_->observeState(ui::microphoneRow,lParam!=0);
            return 0;

        case kFansStatusMessage: {
            const RECT status = statusBarRect();
            InvalidateRect(window_, &status, FALSE);
            return 0;
        }
        case kDeviceOperationMessage:
            if(nvidiaPage_)nvidiaPage_->refreshCommandAvailability();
            if(radeonPage_)radeonPage_->refreshCommandAvailability();
            if (settingsPage_) settingsPage_->refreshCommandAvailability();
            if (fansPage_) fansPage_->refreshCommandAvailability();
            if (keysPage_) keysPage_->refreshCommandAvailability();
            return 0;

        case WM_DESTROY:
            onDestroy();
            return 0;

        default:
            break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void MainWindow::onCreate() {
    ui::footerStatuses().clear();
    ui::footerStatuses().observe([window=window_]{PostMessageW(window,WM_APP+52,0,0);});
    dpi_ = GetDpiForWindow(window_);
    if (dpi_ == 0) {
        dpi_ = 96;
    }

    RECT client{};
    GetClientRect(window_, &client);
    width_ = client.right - client.left;
    height_ = client.bottom - client.top;

    createFonts();

    // The standard caption must go, not merely the non-client geometry.
    //
    // Returning 0 from WM_NCCALCSIZE stops Windows from reserving space for the
    // caption, but the desktop window manager still paints the system title bar
    // and its buttons over the client area while WS_CAPTION is set, which showed
    // up as a second title bar above the custom one. Removing the style is what
    // actually stops that drawing.
    //
    // WS_THICKFRAME and WS_MAXIMIZEBOX stay: the first keeps the drop shadow and
    // the maximise animation, the second keeps the window eligible for snap
    // layouts. Resizing is driven by WM_NCHITTEST, and the custom maximise button
    // toggles the state itself.
    const LONG_PTR style = GetWindowLongPtrW(window_, GWL_STYLE);
    SetWindowLongPtrW(window_, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_CAPTION));

    // Style changes do not take effect until the frame is recalculated.
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // Keep the DWM frame out of the painted client area. Extending it by one
    // pixel produced a light hairline above the custom title bar on Windows 11.
    MARGINS margins{0, 0, 0, 0};
    static_cast<void>(DwmExtendFrameIntoClientArea(window_, &margins));

    // A dark frame so the shadow reads correctly against the dark surface, and
    // the immersive dark title bar so system-drawn parts do not flash white.
    const DWORD cornerPreference = 1;  // DWMWCP_DONOTROUND
    static_cast<void>(DwmSetWindowAttribute(window_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                                            &cornerPreference, sizeof(cornerPreference)));
    const BOOL dark = TRUE;
    static_cast<void>(DwmSetWindowAttribute(window_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark,
                                            sizeof(dark)));
    const COLORREF borderColor = theme::kTitleBar;
    static_cast<void>(DwmSetWindowAttribute(window_, 34 /*DWMWA_BORDER_COLOR*/, &borderColor,
                                            sizeof(borderColor)));
    const COLORREF captionColor = theme::kTitleBar;
    static_cast<void>(DwmSetWindowAttribute(window_, 35 /*DWMWA_CAPTION_COLOR*/, &captionColor,
                                            sizeof(captionColor)));

    std::string log = "client rect=";
    log += std::to_string(width_);
    log += "x";
    log += std::to_string(height_);
    log += " dpiForWindow=";
    log += std::to_string(GetDpiForWindow(window_));
    appendStartupLog(log);

    // Dependency status is separate from EC identity and never starts a driver.
    try {
        const auto status = dependencies::pawnIoDriverStatus(dependencies::PawnIoVersionQuery::LoadedDriver);
        appendStartupLog(std::format("PawnIO startup: installed={} running={} service_state={} win32_exit_code={} service_exit_code={} file_version={} loaded_version={} driver_version={} version_source=[{}] image_path=[{}] version_status=[{}] loaded_version_status=[{}] bundled_setup_version=2.2.0",
            status.installed, status.running, status.serviceState, status.win32ExitCode, status.serviceExitCode,
            status.fileVersion.empty()?"unavailable":status.fileVersion,status.loadedVersion.empty()?"unavailable":status.loadedVersion,
            status.version().empty()?"unavailable":status.version(),status.versionSource(),
            mc::windows::encoding::utf8(status.resolvedBinaryPath),status.versionStatus,status.loadedVersionStatus));
        if (!status.installed) pawnIoStartupMessage_ = L"Драйвер PawnIO не установлен.\n\nУстановите его кнопкой «Установить драйвер PawnIO» на вкладке INFO.";
        else if (!status.running) pawnIoStartupMessage_ = L"Драйвер PawnIO установлен, но его служба не запущена.\n\nФункции PawnIO будут недоступны до запуска службы. Проверьте службу PawnIO в Windows.";
    } catch (const std::exception& error) {
        appendStartupLog("PawnIO startup: status unknown: " + std::string(error.what()));
        pawnIoStartupMessage_ = L"Не удалось проверить драйвер PawnIO.\n\n" + mc::windows::encoding::wide(error.what());
    }

    BoardDescriptor startupBoard=readBoardDescriptor();
    bool identityRead{true};
    if (!noEc_&&!nativeBatteryProfile(deviceProfile(startupBoard).id)&&!l16Profile(deviceProfile(startupBoard).id)&&deviceProfile(startupBoard).id!="minisforum-hpbsd"&&deviceProfile(startupBoard).id!="lenovo-legion-kwcn") {
    // Open the embedded controller. A failure is not fatal: the interface is
    // still useful for reading the board identity, and the status strip explains
    // what is missing instead of the window refusing to appear.
    try {
        ec_ = std::make_unique<EcAccess>(createDefaultBackend());
        const BoardCheck& check = ec_->verifyBoard();
        startupBoard=check.board;identityRead=true;
        if (check.verified) {
            sensorStatus_.clear();
            appendStartupLog("ec: " + describeBoardCheck(check));
        } else {
            sensorStatus_ = L"Плата не подтверждена — показания недоступны";
            appendStartupLog("ec: board check failed: " + check.error);
        }
    } catch (const BackendUnavailable& error) {
        sensorStatus_ = deviceProfile(startupBoard).id=="eluktronics-hydroc16g2"?
            L"HYDROC: чтение EC по кнопке «Снять дамп» на вкладке INFO":L"Драйвер EC недоступен";
        appendStartupLog(std::string("ec: ") + error.what() + " | " + error.hint());
    } catch (const std::exception& error) {
        sensorStatus_ = L"Ошибка доступа к EC";
        appendStartupLog(std::string("ec: ") + error.what());
    }

    } else if(noEc_) {
        sensorStatus_ = L"Просмотр INFO без доступа к EC";
        appendStartupLog("EC disabled by --no-ec");
    } else if(deviceProfile(startupBoard).id=="minisforum-hpbsd") {
        sensorStatus_.clear();
        appendStartupLog("HPBSD: native AMD/PawnIO and ITE5571 telemetry; no Uniwill controller or startup restore");
    } else if(l16Profile(deviceProfile(startupBoard).id)) {
        sensorStatus_.clear();
        appendStartupLog("L16 typed RAM controller; EC Project1A / BIOS ID6 / driver guards; legacy X5 restore not selected");
    } else {
        sensorStatus_.clear();
        appendStartupLog("Typed native board controller; legacy X5 transport/restore not selected");
    }
    if(!identityRead)startupBoard=readBoardDescriptor();
    overclockAllowed_=mc::overclockObservationSupported(startupBoard);
    if(overclockAllowed_){
        overclock_=std::make_unique<mc::OverclockOperations>();
        overclockOff_=CreateWindowW(L"BUTTON",L"Выкл. 0",WS_CHILD|WS_DISABLED|BS_PUSHBUTTON,0,0,0,0,window_,nullptr,instance_,nullptr);
        overclockOn_=CreateWindowW(L"BUTTON",L"Вкл. 1",WS_CHILD|WS_DISABLED|BS_PUSHBUTTON,0,0,0,0,window_,nullptr,instance_,nullptr);
    }
    beginBatteryIdentityHistory(ec_&&ec_->boardVerified()&&deviceProfile(startupBoard).id=="wu-jie-15-xpro");
    if(!noEc_&&(deviceProfile(startupBoard).id=="huawei-hlyl-wxx9"||ramFanControlProfile(deviceProfile(startupBoard).id)))sensorStatus_.clear();
    const auto header=ui::deviceHeader(startupBoard);
    brandManufacturer_=mc::windows::encoding::wide(header.manufacturer);brandModel_=mc::windows::encoding::wide(header.model);
    measureBrand();
    appendStartupLog("Device header: manufacturer="+header.manufacturer+" model="+header.model+"; startup SMBIOS");
    osd_ = std::make_unique<OsdPopup>();
    osd_->create(instance_, appendStartupLog);
    osd_->setEnabled(!preferences_.osdDisabled);
    systemEvents_ = std::make_unique<mc::windows::OsdSystemMonitor>(
        [this](mc::SystemEvent event) {
            if (osd_) {
                auto message=osd::system(event,"Windows");
                if (event.kind==mc::SystemEventKind::Camera) {
                    if (event.value==-1) {
                        const auto token=osd_->beginOperation(std::move(message));
                        std::lock_guard lock(cameraOsdGate_);cameraOsdToken_=token;cameraOsdAt_=event.receivedAt;
                    } else {
                        std::uint64_t token{};
                        {std::lock_guard lock(cameraOsdGate_);if(cameraOsdAt_==event.receivedAt){token=cameraOsdToken_;cameraOsdToken_=0;}}
                        if(token)osd_->completeOperation(token,std::move(message));
                        else osd_->show(std::move(message));
                    }
                } else osd_->show(std::move(message));
            }
            if (window_) PostMessageW(window_,kSystemStateMessage,static_cast<WPARAM>(event.kind),
                event.kind==mc::SystemEventKind::Microphone?(event.muted?0:1):event.value);
        },
        appendStartupLog,
        [target=window_] { PostMessageW(target,kKeyboardActivityMessage,0,0); });
    powerDiagnostics_ = std::make_unique<mc::windows::PowerDiagnostics>(window_);
    if (loggingEnabled()) {
        const auto path = powerDiagnostics_->path().u8string();
        appendStartupLog("Power diagnostics: " + std::string(reinterpret_cast<const char*>(path.data()), path.size()));
    }
    nvidiaPresent_=nvidiaGpuPresent();radeonPresent_=radeonDiscretePresent();
    if(!ui::showNvidiaTab(nvidiaPresent_)&&activeTab_==Tab::Nvidia)activeTab_=Tab::Fans;
    deviceOperations().setNotifier([window = window_] { PostMessageW(window, kDeviceOperationMessage, 0, 0); });
    std::shared_ptr<mc::windows::FanController> controller;
    if (ec_ && ec_->boardVerified() && ec_->info().writable) controller = std::make_shared<mc::windows::FanController>(*ec_);
    fanController_ = controller;
    biosPage_=std::make_unique<BiosPage>(window_,ec_,
        windows::makeInfoMaximumFanProbe(controller),windows::makeInfoFirmwareFanDefault(controller),osd_.get());
    biosPage_->arrange(contentRect(), dpi_, activeTab_ == Tab::Bios);
    settingsPage_ = std::make_unique<SettingsPage>(window_, ec_.get(), osd_.get(), appendStartupLog, controller);
    settingsPage_->arrange(contentRect(), dpi_, activeTab_ == Tab::Sets);
    keysPage_ = std::make_unique<KeysPage>(window_, ec_.get(), preferences_,
        [this](const ApplicationPreferences& preferences) { applyPreferences(preferences); }, osd_.get());
    keysPage_->arrange(contentRect(), dpi_, activeTab_ == Tab::Keys);
    fansPage_ = std::make_unique<FansPage>(window_, ec_.get(), osd_.get(), appendStartupLog, controller);
    fansPage_->arrange(contentRect(), dpi_, activeTab_ == Tab::Fans);
    if(ui::showNvidiaTab(nvidiaPresent_))nvidiaPage_=std::make_unique<DiscreteGpuPage>(window_,GpuVendor::Nvidia,osd_.get());
    if(ui::showRadeonTab(radeonPresent_))radeonPage_=std::make_unique<DiscreteGpuPage>(window_,GpuVendor::Radeon,osd_.get());
    if(nvidiaPage_)nvidiaPage_->arrange(contentRect(),dpi_,activeTab_==Tab::Nvidia);
    if(radeonPage_)radeonPage_->arrange(contentRect(),dpi_,activeTab_==Tab::Radeon);
    if(!noEc_)nativeProfileController_=windows::gm6Controller();
    if(nativeProfileController_)nativeProfileInbox_=std::make_shared<NativeProfileInbox>();
    oemEvents_ = std::make_unique<mc::windows::OemEventMonitor>(
        [this,nativeGm6=static_cast<bool>(nativeProfileController_)](const std::string& provider, std::uint64_t code) {
            if (provider == "WmiMonitorBrightnessEvent") {
                if (systemEvents_ && code <= 100) systemEvents_->brightnessFallback(static_cast<int>(code));
                return;
            }
            if (provider != "AcpiTest_EventULong") return;
            const auto at = osd::now();
            const bool cameraOwned = code==207 && osd_ && osd_->hasPendingCommand("switch-Camera");
            if (osd_) {
                if (code >= 59 && code <= 63) osd_->show(osd::keyboard(static_cast<int>(code - 59), "", at,deviceProfile(readBoardDescriptor()).keyboardBrightnessMaximum));
                else if (code == 64 || code == 65)
                    osd_->show({"switch-Windows", code == 64 ? "Windows заблокирована" : "Windows разблокирована", "Клавиша Windows", osd::Icon::Windows, {}, false, false, at});
                else if (code == 165 || code == 184)
                    osd_->show({code == 184 ? "switch-Fn" : "switch-Windows", code == 184 ? "Блокировка клавиши Fn" : "Блокировка клавиши Windows", "Событие аппаратной клавиши", code == 184 ? osd::Icon::Keyboard : osd::Icon::Windows, {}, false, false, at});
                else if (code == 179 || code == 180)
                    osd_->show({"keyboard", "Подсветка клавиатуры", "Событие аппаратной клавиши", osd::Icon::Keyboard, {}, false, false, at});
                else if (code == 167)
                    osd_->show({"fans","Вентиляторы","Событие аппаратной клавиши",osd::Icon::Fan,{},false,false,at});
                else if ((code == 176&&!nativeGm6) || code == 188)
                    osd_->show({"power", "Производительность", "Событие аппаратной клавиши", osd::Icon::Power, {}, false, false, at});
                else if (code == 164)
                    osd_->show({"switch-Wifi", "Wi-Fi", "Событие аппаратной клавиши", osd::Icon::Wifi, {}, false, false, at});
                else if (code == 207 && !cameraOwned)
                    osd_->show({"switch-Camera", "Веб-камера", "Проверяю состояние Windows…", osd::Icon::Camera, {}, false, true, at});
                // Rotation starts its own tracked operation in the handler.
                // A second untracked pending notice would orphan the first token.
            }
            if (systemEvents_) {
                // On the actual Wujie keyboard B7 is the touchpad key (owner
                // report + current event log). GM OEM183 is MicMute. Do not
                // copy that OEM meaning across boards or force a mic OSD.
                if (code == 4 || code == 5 || (code==183&&deviceProfile(readBoardDescriptor()).id=="wu-jie-15-xpro")) systemEvents_->touchpadHotkey();
                else if (code == 183&&nativeGm6) systemEvents_->audioHotkey(true);
                else if (code == 207 && !cameraOwned) systemEvents_->cameraHotkey(at);
            }
            if (window_ != nullptr) PostMessageW(window_, kOemEventMessage, static_cast<WPARAM>(code), static_cast<LPARAM>(at));
        },
        [this](std::string line) { appendStartupLog(line); },
        [controller=nativeProfileController_](std::string provider){if(controller&&provider=="AcpiTest_EventULong")controller->nativeHotkeySubscribed();});

    if (controller) {
        configurationRestore_ = std::make_unique<mc::windows::ConfigurationRestore>(*ec_, controller);
        controller->setConfigurationRestoreNotifier([window = window_] { PostMessageW(window, kConfigurationRestoreMessage, 0, 0); });
    }
    restoreConfiguration("startup"); // Includes generic KEYs on unknown/no-EC devices.
    if (wcsstr(GetCommandLineW(), L"--osd-preview") != nullptr)
        osd_->show(osd::keyboard(2, "Предпросмотр уведомления · параметры не меняются", osd::now()));

}

void MainWindow::requestExit() {
    suspendLiquidCooling();
    if(exiting_)return;
    exiting_=true;
    if(trayMenuTracking_){EndMenu();trayMenuTracking_=false;}
    if(window_)DestroyWindow(window_);
}
void MainWindow::onDestroy() {
    suspendLiquidCooling();
    ui::footerStatuses().observe({});
    deviceOperations().setNotifier({});
    exiting_ = true;
    if (trayMenuTracking_) { EndMenu(); trayMenuTracking_ = false; }
    KillTimer(window_, kTrayRefreshTimer);
    removeTrayIcon();
    if (trayMenuOwner_) { DestroyWindow(trayMenuOwner_); trayMenuOwner_ = nullptr; }
    oemEvents_.reset();
    closeNativeProfileCommands();
    if(screenFlip_)screenFlip_->shutdown();
    systemEvents_.reset();
    powerDiagnostics_.reset();
    overclock_.reset();
    if (fanController_) fanController_->setConfigurationRestoreNotifier({});
    configurationRestore_.reset();
    fansPage_.reset();
    nvidiaPage_.reset();
    keysPage_.reset();
    settingsPage_.reset();
    fanController_.reset();
    biosPage_.reset();
    nvidiaPage_.reset();radeonPage_.reset();
    osd_.reset();
    PostQuitMessage(0);
}

void MainWindow::updatePageVisibility(bool visible) {
    const auto arrange = [this](Tab tab, bool show) {
        const auto area = contentRect();
        std::string_view pageName;bool invoked=true;
        switch (tab) {
        case Tab::Bios: pageName="INFO";if (biosPage_) biosPage_->arrange(area, dpi_, show);else invoked=false;break;
        case Tab::Sets: pageName="SETs";if (settingsPage_) settingsPage_->arrange(area, dpi_, show);else invoked=false;break;
        case Tab::Keys: pageName="KEYs";if (keysPage_) keysPage_->arrange(area, dpi_, show);else invoked=false;break;
        case Tab::Fans: pageName="FANs";if (fansPage_) fansPage_->arrange(area, dpi_, show);else invoked=false;break;
        case Tab::Nvidia: pageName="NVIDIA";if (nvidiaPage_) nvidiaPage_->arrange(area, dpi_, show && ui::showNvidiaTab(nvidiaPresent_));else invoked=false;break;
        case Tab::Radeon: pageName="Radeon";if(radeonPage_)radeonPage_->arrange(area,dpi_,show&&ui::showRadeonTab(radeonPresent_));else invoked=false;break;
        case Tab::OvClock: {
            pageName="OvClock";
            arrangeOverclock(show);
            break;
        }
        }
        if(invoked)ui::DpiChangeTrace::pageDone(pageName,show);
    };
    // Prepare and paint the destination above the outgoing page before hiding it.
    if (visible) arrange(activeTab_, true);
    for (const auto tab : {Tab::Bios, Tab::Sets, Tab::Keys, Tab::Fans, Tab::Nvidia, Tab::Radeon, Tab::OvClock})
        if (!visible || tab != activeTab_) arrange(tab, false);
}

void MainWindow::restoreKeysApplicationFlags(){
    if(!osd_)return;
    try{auto desired=preferences_;windows::readKeysApplicationPreferences(desired);
        if(desired!=preferences_)applyPreferences(desired);
    }catch(const std::exception& e){appendStartupLog(std::string("KEYS application switches: ")+e.what());}
}
void MainWindow::restoreConfiguration(const std::string& reason) {
    if(exiting_)return;
    // Application KEYs preferences are independent of board/driver admission.
    try{auto application=preferences_;std::string error;
        if(!windows::readStoredApplicationPreferences(application,error)&&!error.empty())appendStartupLog("Application read failed: "+error);
        application.osdDisabled=preferences_.osdDisabled;
        windows::readKeysApplicationPreferences(application);
        if(application!=preferences_)applyPreferences(application);
    }catch(const std::exception& e){appendStartupLog(std::string("KEYS application restore failed: ")+e.what());}
    if(keysPage_)keysPage_->restore(reason);
    if(reason=="system_sleep_clock")restoreKeysApplicationFlags();
    if(!noEc_&&!exiting_&&(reason=="system_sleep_clock"||reason=="startup"))
        windows::restoreNativeBatteryForPower(reason);
    if(exiting_||noEc_)return;
    if(windows::restoreDeviceConfiguration(reason,configurationRestore_.get())){
        auto* settings=realSettingsPage_?realSettingsPage_.get():settingsPage_.get();
        if(settings)settings->restoreKeyboardCycle();
    }
}

void MainWindow::applyApplicationIcons() {
    iconLarge_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_MAGICCONTROL), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXICON, dpi_), GetSystemMetricsForDpi(SM_CYICON, dpi_), LR_SHARED));
    iconSmall_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_MAGICCONTROL), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXSMICON, dpi_), GetSystemMetricsForDpi(SM_CYSMICON, dpi_), LR_SHARED));
    iconHeader_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_MAGICCONTROL), IMAGE_ICON,
        scale(kHeaderIconSize), scale(kHeaderIconSize), LR_SHARED));
    SendMessageW(window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconLarge_));
    SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall_));
    if (trayAdded_) addTrayIcon();
}

bool MainWindow::addTrayIcon() {
    if (exiting_ || !window_ || !iconSmall_) return false;
    NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = window_; data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kTrayMessage; data.hIcon = iconSmall_;
    wcscpy_s(data.szTip, L"MagicControl");
    const bool adding = !trayAdded_;
    if (!Shell_NotifyIconW(adding ? NIM_ADD : NIM_MODIFY, &data)) {
        trayAdded_ = false;
        appendStartupLog("Tray icon: Shell_NotifyIcon failed " + std::to_string(GetLastError()));
        return false;
    }
    trayAdded_ = true;
    if (adding) {
        data.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &data);
        if (taskbarCreated_) ChangeWindowMessageFilterEx(window_, taskbarCreated_, MSGFLT_ALLOW, nullptr);
        ChangeWindowMessageFilterEx(window_, kTrayMessage, MSGFLT_ALLOW, nullptr);
        appendStartupLog("Tray icon registered; application icon embedded.");
        if (settingsPage_) settingsPage_->refreshForTray();
    }
    return true;
}

void MainWindow::removeTrayIcon() {
    if (!trayAdded_ || !window_) return;
    NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = window_; data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data); trayAdded_ = false;
}

void MainWindow::hideToTray() {
    if (exiting_) return;
    if (!trayAdded_ && !addTrayIcon()) { ShowWindow(window_, SW_MINIMIZE); return; }
    if (!IsIconic(window_)) trayRestoreMaximized_ = IsZoomed(window_) != FALSE;
    hiddenToTray_ = true;
    updatePageVisibility(false);
    ShowWindow(window_, SW_HIDE);
    appendStartupLog("Window hidden to tray; active fan controller and notifications retained.");
}

void MainWindow::openFromTray() {
    if (exiting_) return;
    hiddenToTray_ = false;
    ShowWindow(window_, trayRestoreMaximized_ ? SW_SHOWMAXIMIZED : SW_RESTORE);
    updatePageVisibility(true);
    applyApplicationIcons();
    SetForegroundWindow(window_);
    appendStartupLog("Window restored from tray.");
}

void MainWindow::showTrayMenu(POINT anchor) {
    if (exiting_ || trayMenuTracking_) return;
    if (!trayMenuOwner_) {
        WNDCLASSW klass{}; klass.lpfnWndProc = DefWindowProcW; klass.hInstance = instance_;
        klass.lpszClassName = L"MagicControl.TrayMenuOwner";
        RegisterClassW(&klass);
        trayMenuOwner_ = CreateWindowExW(WS_EX_TOOLWINDOW, klass.lpszClassName, L"MagicControl menu",
            WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, nullptr);
    }
    if (!trayMenuOwner_) return;
    const auto state = settingsPage_ ? settingsPage_->trayState() : TraySettingsState{};
    HMENU menu = CreatePopupMenu(), battery = CreatePopupMenu(), power = CreatePopupMenu(), keyboard = CreatePopupMenu();
    if (!menu || !battery || !power || !keyboard) {
        for (HMENU item : {menu,battery,power,keyboard}) if (item) DestroyMenu(item);
        return;
    }
    const auto append = [](HMENU parent, UINT id, const wchar_t* caption, bool enabled, bool checked) {
        AppendMenuW(parent, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED) | (checked ? MF_CHECKED : MF_UNCHECKED), id, localized(caption).c_str());
    };
    const auto powerProfileId=deviceProfile(ui::presentationBoard()).id;
    const auto profiles=ui::cpuPowerLabels(powerProfileId);
    for (int i = 0; i < ui::batteryThresholdCount; ++i) {
        const auto caption = std::to_wstring(ui::batteryThresholdValues[i]) + L"%";
        append(battery,kTrayBattery+i,caption.c_str(),state.ready,state.battery==i);
    }
    for (int i = 1; i <= stockCpuProfileCount(powerProfileId); ++i) {
        const auto caption=mc::windows::encoding::wide(profiles[i]);
        append(power,kTrayPower+i,caption.c_str(),state.performanceReady && (i != 3 || state.turboAvailable),state.performance==i);
    }
    AppendMenuW(power,MF_SEPARATOR,0,nullptr);
    append(power,kTrayBattery+ui::batteryAutomaticIndex,state.turboAvailable ? L"Turbo EC" : L"Limiter EC",state.apuPolicyReady,state.batteryAuto);
    for (int level = 1; level <= 4; ++level) {
        const auto caption = L"Уровень " + std::to_wstring(level);
        append(keyboard,kTrayBrightness+level,caption.c_str(),state.ready&&state.keyboardSupported,state.brightness==level);
    }
    AppendMenuW(keyboard,MF_SEPARATOR,0,nullptr);
    append(keyboard,kTrayBrightness,L"Выкл.",state.ready&&state.keyboardSupported,state.brightness==0);
    append(menu,kTrayOpen,L"Открыть",true,false); SetMenuDefaultItem(menu,kTrayOpen,FALSE);
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(battery),localized(L"Батарея").c_str());
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(power),localized(L"Мощность").c_str());
    AppendMenuW(menu,MF_POPUP | (state.keyboardSupported ? MF_ENABLED : MF_GRAYED),reinterpret_cast<UINT_PTR>(keyboard),localized(L"Подсветка").c_str());
    append(menu,kTrayExit,L"Выйти",true,false);
    // The first frame uses the cached snapshot. EC refresh never blocks opening.
    if (settingsPage_) settingsPage_->refreshForTray();
    // The hidden owner must share the anchor monitor's DPI before menu measure.
    SetWindowPos(trayMenuOwner_,nullptr,anchor.x,anchor.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    SetForegroundWindow(trayMenuOwner_);
    trayMenuTracking_ = true;
    const UINT selected = TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_RIGHTALIGN|TPM_BOTTOMALIGN,
        anchor.x,anchor.y,trayMenuOwner_,nullptr);
    trayMenuTracking_ = false;
    if (selected) mc::windows::recordPowerAction("tray_command", "requested", static_cast<int>(selected));
    DestroyMenu(menu); PostMessageW(trayMenuOwner_,WM_NULL,0,0);
    if (selected == kTrayOpen) openFromTray();
    else if (selected == kTrayExit) requestExit();
    else if (settingsPage_) {
        if (selected >= kTrayBattery && selected <= kTrayBattery+ui::batteryAutomaticIndex)
            settingsPage_->applyFromTray(TraySetting::Battery,static_cast<int>(selected-kTrayBattery));
        else if (selected > kTrayPower && selected <= kTrayPower+stockCpuProfileCount(powerProfileId))
            settingsPage_->applyFromTray(TraySetting::Performance,static_cast<int>(selected-kTrayPower));
        else if (selected >= kTrayBrightness && selected <= kTrayBrightness+4)
            settingsPage_->applyFromTray(TraySetting::Brightness,static_cast<int>(selected-kTrayBrightness));
    }
}

void MainWindow::applyPreferences(const ApplicationPreferences& preferences) {
    const auto settingsPath = windows::applicationPreferencesPath();
    windows::saveApplicationPreferencesIfChanged(settingsPath,preferences_,preferences);
    const bool reopen=!anyLoggingEnabled()&&(preferences.allLogs||preferences.batteryLog);
    const bool diagnosticsChanged=preferences_.batteryLog!=preferences.batteryLog;
    preferences_ = preferences;
    if(reopen)static_cast<void>(initializeLogging(preferences_.allLogs,{},preferences_.batteryLog));
    else{setLoggingEnabled(preferences_.allLogs);setBatteryLoggingEnabled(preferences_.batteryLog);}
    if(diagnosticsChanged)updatePageVisibility(IsWindowVisible(window_)!=FALSE);
    osd_->setEnabled(!preferences_.osdDisabled);
}

void MainWindow::handleOemEvent(std::uint64_t code, std::uint64_t receivedAt) {
    if ((code==64 || code==65) && keysPage_) keysPage_->observeState(0,code==64);
    if (code >= 59 && code <= 63) {
        auto* actualSettings=realSettingsPage_?realSettingsPage_.get():settingsPage_.get();
        if (actualSettings) actualSettings->observeKeyboardLevel(static_cast<int>(code - 59));
        return; // Feedback was already delivered on the independent OSD thread.
    } else if (code == 208) {
        startScreenFlip(receivedAt);
    } else if (code == 207) {
        return; // Windows device notifications finish the camera observation.
    } else if (code == 4 || code == 5) {
        return;
    } else if (code == 164) {
        const auto state = mc::windows::readDevice(mc::windows::DeviceKind::Wifi);
        if (osd_) osd_->show({"switch-Wifi", "Wi-Fi", state.enabled.has_value() ? (*state.enabled ? "Включён" : "Выключен") : "Состояние Windows не подтверждено", osd::Icon::Wifi,{},false,false,receivedAt});
    } else if (code==176&&nativeProfileController_) {
        queueNativeProfileCycle(receivedAt);return;
    } else if (code == 167 || code == 176 || code == 188) {
        return;
    } else if (code == 186) {
        openFromTray(); return;
    } else {
        return;
    }
    const RECT status = statusBarRect();
    InvalidateRect(window_, &status, FALSE);
}

void MainWindow::queueNativeProfileCycle(std::uint64_t receivedAt){
    if(exiting_||noEc_||!nativeProfileController_||!nativeProfileInbox_)return;
    const auto token=osd_?osd_->beginOperation({"gm6-native-profile","Штатный профиль","Переключаю Office / Gaming / Turbo / Custom…",osd::Icon::Power,{},false,true,receivedAt}):0;
    const auto inbox=nativeProfileInbox_;const HWND window=window_;
    nativeProfileController_->enqueueNativeProfileCycle(receivedAt,[inbox,window,token](windows::Gm6NativeProfileResult result){
        {std::lock_guard lock(inbox->gate);if(inbox->closed)return;inbox->results.emplace_back(token,std::move(result));}
        PostMessageW(window,kNativeProfileFinishedMessage,0,0);
    });
}
void MainWindow::finishNativeProfileCommands(){
    if(exiting_||!nativeProfileInbox_)return;
    std::deque<std::pair<std::uint64_t,windows::Gm6NativeProfileResult>> results;
    {std::lock_guard lock(nativeProfileInbox_->gate);results.swap(nativeProfileInbox_->results);}
    for(auto& [token,result]:results){
        const auto title=result.readbackConfirmed&&result.observed?std::string(gm6::nativeProfileName(*result.observed)):"Штатный профиль не подтверждён";
        if(osd_)osd_->completeOperation(token,{"gm6-native-profile",title,result.status,osd::Icon::Power,{},!result.readbackConfirmed,false,result.receivedAt});
        appendStartupLog(std::string(gmModelName(deviceProfile(readBoardDescriptor()).id))+" physical mode button: "+result.status);
    }
    if(settingsPage_)settingsPage_->refreshForTray(true);
    if(realSettingsPage_)realSettingsPage_->refreshForTray(true);
    if(fansPage_)fansPage_->refreshDeviceState();
    if(realFansPage_)realFansPage_->refreshDeviceState();
}
void MainWindow::closeNativeProfileCommands(){
    if(nativeProfileInbox_){std::lock_guard lock(nativeProfileInbox_->gate);nativeProfileInbox_->closed=true;nativeProfileInbox_->results.clear();}
    if(nativeProfileController_)nativeProfileController_->cancelNativeProfileCommands();
    nativeProfileInbox_.reset();nativeProfileController_.reset();
}

void MainWindow::startScreenFlip(std::uint64_t receivedAt) {
    if(exiting_)return;
    if(!screenFlip_)screenFlip_=std::make_unique<DisplayRotationOperations>(
        windows::toggleBuiltInDisplayRotation,
        [this](std::uint64_t at){return osd_?osd_->beginOperation({"display-rotation","Поворот экрана","Поворачиваю на 180°…",osd::Icon::Display,{},false,true,at}):0;},
        [target=window_]{PostMessageW(target,kScreenFlipFinishedMessage,0,0);});
    screenFlip_->request(receivedAt);
}

void MainWindow::completeScreenFlip() {
    if(!screenFlip_)return;const auto result=screenFlip_->take();if(!result||exiting_)return;
    osd::Message notice{"display-rotation","Поворот экрана",result->detail,osd::Icon::Display,{},!result->success,false,result->receivedAt};
    sensorStatus_=result->success?L"":L"Sc: поворот не выполнен";
    appendStartupLog(std::string("Sc rotation ")+(result->success?"confirmed: ":"failed: ")+result->detail);
    if(osd_)osd_->completeOperation(result->token,std::move(notice));
    screenFlip_->continueQueued();
}

void MainWindow::onDpiChanged(int newDpi) {
    dpi_ = newDpi != 0 ? static_cast<UINT>(newDpi) : 96;
    destroyFonts();
    createFonts();
    ui::DpiChangeTrace::mark(ui::DpiTraceStage::FontsReady);
    applyApplicationIcons();
    ui::DpiChangeTrace::mark(ui::DpiTraceStage::IconsReady);
    updatePageVisibility(!hiddenToTray_ && !IsIconic(window_) && IsWindowVisible(window_));
    repaint(window_);
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

void MainWindow::createFonts() {
    fontBrand_ = createUiFont(ui::FontRole::Brand, dpi_);
    fontTab_ = createUiFont(ui::FontRole::Tab, dpi_);
    fontTabActive_ = createUiFont(ui::FontRole::TabActive, dpi_);
    fontStatus_ = createUiFont(ui::FontRole::Body, dpi_);
    measureBrand();
}
void MainWindow::measureBrand() {
    HDC dc=GetDC(window_);if(!dc)return;
    const auto measure=[&](const std::wstring& value,HFONT font,int& measuredWidth,ui::TextMetrics& metrics) {
        const auto previous=SelectObject(dc,font);SIZE size{};TEXTMETRICW native{};
        ::mc::gui::localizedTextExtent(dc,value.c_str(),static_cast<int>(value.size()),&size);GetTextMetricsW(dc,&native);
        SelectObject(dc,previous);measuredWidth=static_cast<int>(size.cx);
        metrics={static_cast<int>(native.tmHeight),static_cast<int>(native.tmAscent)};
    };
    measure(brandManufacturer_,fontBrand_,brandManufacturerWidth_,brandMetrics_);
    measure(brandModel_,fontStatus_,brandModelWidth_,modelMetrics_);
    ReleaseDC(window_,dc);
}
// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

int MainWindow::scale(int logicalPixels) const noexcept {
    return MulDiv(logicalPixels, static_cast<int>(dpi_), 96);
}

RECT MainWindow::titleBarRect() const noexcept {
    return RECT{0, 0, width_, scale(kTitleBarHeight)};
}

RECT MainWindow::brandRect() const noexcept {
    const int naturalWidth=scale(32)+brandManufacturerWidth_+(brandModel_.empty()?0:scale(8)+brandModelWidth_);
    const auto g=ui::headerGeometry(width_,height_,tabWidths(),{static_cast<int>(dpi_)},naturalWidth,visibleTabCount(),ui::visibleGpuTabs(nvidiaPresent_,radeonPresent_,overclockAllowed_));
    return toNativeRect(g.brand);
}

RECT MainWindow::brandMenuRect() const noexcept {
    RECT rect = brandRect();
    const auto text = ui::deviceBrandGeometry(fromNativeRect(rect), {static_cast<int>(dpi_)},
        brandManufacturerWidth_, brandModelWidth_, brandMetrics_, modelMetrics_);
    // Match the visible gap after the arrow on the left of the manufacturer.
    // Share the same inset for hover/hit testing/popup; retain the logo gutter.
    // Keep brandRect unchanged so the actual header text never moves.
    const int rightInset = std::max(0, static_cast<int>(rect.right) - text.arrowX - scale(4));
    rect.left = std::max<LONG>(rect.left, text.manufacturer.left - rightInset);
    return rect;
}

int MainWindow::visibleTabCount() const noexcept{return kTabCount;}
std::array<int,kTabCount> MainWindow::tabWidths() const noexcept {
    auto values=ui::tabTextWidths(dpi_);HDC dc=GetDC(window_);
    if(dc){const auto old=SelectObject(dc,fontTabActive_);SIZE size{};
        for(int i=4;i<kTabCount;++i)if(::mc::gui::localizedTextExtent(dc,kTabTitles[i],static_cast<int>(wcslen(kTabTitles[i])),&size))values[i]=size.cx;
        SelectObject(dc,old);ReleaseDC(window_,dc);}
    return values;
}
RECT MainWindow::tabRect(int index) const noexcept {
    if (index < 0 || index >= visibleTabCount() || !ui::visibleGpuTabs(nvidiaPresent_,radeonPresent_,overclockAllowed_)[index]) {
        return RECT{0, 0, 0, 0};
    }

    const auto geometry = ui::headerGeometry(width_, height_,
        tabWidths(), {static_cast<int>(dpi_)},0,visibleTabCount(),ui::visibleGpuTabs(nvidiaPresent_,radeonPresent_,overclockAllowed_));
    return toNativeRect(geometry.tabs[index]);
}

RECT MainWindow::windowButtonRect(int index) const noexcept {
    const int buttonWidth = scale(kWindowButtonWidth);
    const int right = width_ - (kButtonClose - index) * buttonWidth;
    return RECT{right - buttonWidth, 0, right, scale(kTitleBarHeight)};
}

RECT MainWindow::contentRect() const noexcept {
    const int top = scale(kTitleBarHeight);
    const int bottom = height_ - scale(kStatusBarHeight);
    return RECT{0, top, width_, bottom > top ? bottom : top};
}

RECT MainWindow::statusBarRect() const noexcept {
    const int top = height_ - scale(kStatusBarHeight);
    return RECT{0, top > 0 ? top : 0, width_, height_};
}

int MainWindow::tabAt(int x, int y) const noexcept {
    const RECT bar = titleBarRect();
    if (y < bar.top || y >= bar.bottom) {
        return -1;
    }
    for (int tab = 0; tab < visibleTabCount(); ++tab) {
        const RECT rect = tabRect(tab);
        if (x >= rect.left && x < rect.right) {
            return tab;
        }
    }
    return -1;
}

int MainWindow::windowButtonAt(int x, int y) const noexcept {
    const RECT bar = titleBarRect();
    if (y < bar.top || y >= bar.bottom) {
        return -1;
    }
    for (int index = kButtonMinimise; index <= kButtonClose; ++index) {
        const RECT rect = windowButtonRect(index);
        if (x >= rect.left && x < rect.right) {
            return index;
        }
    }
    return -1;
}

bool MainWindow::brandContains(int x, int y) const noexcept {
    const RECT rect = brandMenuRect();
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

LRESULT MainWindow::onNcHitTest(POINT screenPoint) const {
    RECT windowRect{};
    GetWindowRect(window_, &windowRect);

    // A maximised frameless window must not offer resize borders, or the user
    // could drag it off screen.
    const bool maximised = IsZoomed(window_) != 0;
    const int border = maximised ? 0 : scale(kResizeBorder);

    const bool left = screenPoint.x < windowRect.left + border;
    const bool right = screenPoint.x >= windowRect.right - border;
    const bool top = screenPoint.y < windowRect.top + border;
    const bool bottom = screenPoint.y >= windowRect.bottom - border;

    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;

    // Inside the frame the title strip drags the window, except over the tabs,
    // the brand and the window buttons, which the client handles itself.
    POINT clientPoint = screenPoint;
    ScreenToClient(window_, &clientPoint);

    if (clientPoint.y >= 0 && clientPoint.y < scale(kTitleBarHeight)) {
        const bool overControl = windowButtonAt(clientPoint.x, clientPoint.y) >= 0 ||
                                 tabAt(clientPoint.x, clientPoint.y) >= 0 ||
                                 brandContains(clientPoint.x, clientPoint.y);
        return overControl ? HTCLIENT : HTCAPTION;
    }

    return HTCLIENT;
}

void MainWindow::onGetMinMaxInfo(MINMAXINFO* info) const {
    if (info == nullptr) {
        return;
    }
    // The minimum is applied as-is, like the default size, so the two stay
    // consistent: scaling this one but not the other let a window be resized
    // larger than the size it opened at.
    info->ptMinTrackSize.x = kMinWidth;
    info->ptMinTrackSize.y = kMinHeight;

    // Without this a maximised frameless window covers the taskbar, because the
    // standard non-client calculation that normally accounts for it is gone.
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor) != 0) {
        const RECT work = monitor.rcWork;
        const RECT full = monitor.rcMonitor;
        info->ptMaxPosition.x = work.left - full.left;
        info->ptMaxPosition.y = work.top - full.top;
        info->ptMaxSize.x = work.right - work.left;
        info->ptMaxSize.y = work.bottom - work.top;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void MainWindow::onMouseMove(int x, int y) {
    const int tab = tabAt(x, y);
    const int button = windowButtonAt(x, y);
    const bool brand = brandContains(x, y);

    if (tab == hotTab_ && button == hotWindowButton_ && brand == hotBrand_) return;

    // Only the elements whose appearance actually changes are invalidated. This
    // used to repaint the entire window on every hover change, which on the BIOS
    // page meant re-rasterising the whole inventory whenever the pointer crossed
    // a button.
    const int previousTab = hotTab_;
    const int previousButton = hotWindowButton_;
    const bool previousBrand = hotBrand_;

    hotTab_ = tab;
    hotWindowButton_ = button;
    hotBrand_ = brand;

    RECT dirty{};
    const auto addRect = [&dirty](const RECT& rect) {
        if (dirty.right <= dirty.left || dirty.bottom <= dirty.top) {
            dirty = rect;
            return;
        }
        dirty.left = std::min(dirty.left, rect.left);
        dirty.top = std::min(dirty.top, rect.top);
        dirty.right = std::max(dirty.right, rect.right);
        dirty.bottom = std::max(dirty.bottom, rect.bottom);
    };

    // Tabs sit in the title strip.
    if (previousTab >= 0) {
        addRect(tabRect(previousTab));
    }
    if (tab >= 0) {
        addRect(tabRect(tab));
    }
    if (previousBrand) {
        addRect(brandRect());
    }
    if (brand) {
        addRect(brandRect());
    }
    if (previousButton >= 0) {
        addRect(windowButtonRect(previousButton));
    }
    if (button >= 0) {
        addRect(windowButtonRect(button));
    }

    if (dirty.right > dirty.left && dirty.bottom > dirty.top) {
        InvalidateRect(window_, &dirty, FALSE);
    } else {
        repaint(window_);
    }
}

void MainWindow::onMouseLeave() {
    if (hotTab_ == -1 && hotWindowButton_ == -1 && !hotBrand_) {
        return;
    }

    // Same reasoning as onMouseMove: clear the highlight without touching the
    // rest of the window.
    RECT dirty{};
    const auto addRect = [&dirty](const RECT& rect) {
        if (dirty.right <= dirty.left || dirty.bottom <= dirty.top) {
            dirty = rect;
            return;
        }
        dirty.left = std::min(dirty.left, rect.left);
        dirty.top = std::min(dirty.top, rect.top);
        dirty.right = std::max(dirty.right, rect.right);
        dirty.bottom = std::max(dirty.bottom, rect.bottom);
    };

    if (hotTab_ >= 0) {
        addRect(tabRect(hotTab_));
    }
    if (hotBrand_) {
        addRect(brandRect());
    }
    if (hotWindowButton_ >= 0) {
        addRect(windowButtonRect(hotWindowButton_));
    }
    hotTab_ = -1;
    hotWindowButton_ = -1;
    hotBrand_ = false;

    if (dirty.right > dirty.left && dirty.bottom > dirty.top) {
        InvalidateRect(window_, &dirty, FALSE);
    } else {
        repaint(window_);
    }
}

void MainWindow::onLeftButtonDown(int x, int y) {
    if(activeTab_==Tab::OvClock&&overclockScroll_.scrollable()){
        const auto area=contentRect();
        if(x>=area.right-scale(12)&&x<area.right&&y>=area.top&&y<area.bottom){
            const auto thumb=page::scrollThumb(area.right-area.left,area.bottom-area.top,overclockScroll_,dpi_,true,false);
            page::scrollPress(window_,overclockScroll_,thumb,y-area.top,overclockDragging_,overclockGrabOffset_);
            arrangeOverclock(true);InvalidateRect(window_,&area,FALSE);return;
        }
    }
    const POINT point{x,y};
    if(PtInRect(&versionRect_,point)){
        if(showUpdateWindow(window_,dpi_)){exiting_=true;DestroyWindow(window_);}
        return;
    }
    const int button = windowButtonAt(x, y);
    if (button >= 0) {
        audit::input("windows.chrome","window_button","\"button\":"+std::to_string(button));
        audit::Scope trace("ui.window_button");
        if (button == kButtonMinimise) {
            ShowWindow(window_, SW_MINIMIZE);
        } else if (button == kButtonMaximise) {
            ShowWindow(window_, IsZoomed(window_) != 0 ? SW_RESTORE : SW_MAXIMIZE);
        } else {
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }
        trace.finish("dispatched");
        return;
    }

    if(brandContains(x,y)) {audit::input("windows.chrome","language_menu_open");audit::Scope trace("ui.language_menu_open");showLanguageMenu();trace.finish("opened");return;}
    const int tab = tabAt(x, y);
    if (tab >= 0) {
        audit::input("windows.chrome","tab_clicked","\"tab\":"+std::to_string(tab)+",\"previous_tab\":"+std::to_string(static_cast<int>(activeTab_)));
        audit::Scope trace("ui.tab_select");
        const auto next = static_cast<Tab>(tab);
        if (next != activeTab_) {
            activeTab_ = next;
            updatePageVisibility(true);
            repaint(window_);
        }
        if(next==Tab::OvClock&&!noEc_&&overclockAllowed_)refreshOverclock();
        trace.finish("selected","\"tab\":"+std::to_string(static_cast<int>(activeTab_)));
    }
}

void MainWindow::onLeftButtonUp(int x, int y) {
    if(overclockDragging_){ReleaseCapture();overclockDragging_=false;const auto area=contentRect();InvalidateRect(window_,&area,FALSE);}
    // Tab and window button actions fire on press, matching the reference.
    static_cast<void>(x);
    static_cast<void>(y);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void MainWindow::fillRect(HDC dc, const RECT& rect, ColorRef color) const {
    HBRUSH brush = const_cast<BrushCache&>(brushes_).get(color);
    if (brush == nullptr) {
        return;
    }
    FillRect(dc, &rect, brush);
}

void MainWindow::drawTextIn(HDC dc, const wchar_t* text, const RECT& rect, HFONT font,
                            ColorRef color, UINT format) const {
    if (text == nullptr || font == nullptr) {
        return;
    }
    HGDIOBJ previousFont = SelectObject(dc, font);
    const int previousMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF previousColor = SetTextColor(dc, color);

    RECT target = rect;
    ::mc::gui::localizedDrawText(dc, text, -1, &target, format | DT_NOPREFIX);

    SetTextColor(dc, previousColor);
    SetBkMode(dc, previousMode);
    SelectObject(dc, previousFont);
}

void MainWindow::clearFrameBuffer() {
    if (frameDc_ && framePrevious_) SelectObject(frameDc_, framePrevious_);
    if (frameBitmap_) DeleteObject(frameBitmap_);
    if (frameDc_) DeleteDC(frameDc_);
    frameDc_ = nullptr; frameBitmap_ = nullptr; framePrevious_ = nullptr;
    frameWidth_ = frameHeight_ = 0;
}

void MainWindow::onPaint() {
    const auto frameStart = std::chrono::steady_clock::now();

    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    paintedRegion_ = paint.rcPaint;

    if (width_ <= 0 || height_ <= 0) {
        EndPaint(window_, &paint);
        return;
    }

    if (!frameDc_ || frameWidth_ != width_ || frameHeight_ != height_) {
        clearFrameBuffer();
        frameDc_ = CreateCompatibleDC(dc);
        if (frameDc_) frameBitmap_ = CreateCompatibleBitmap(dc, width_, height_);
        if (!frameBitmap_) { clearFrameBuffer(); EndPaint(window_, &paint); return; }
        framePrevious_ = SelectObject(frameDc_, frameBitmap_);
        frameWidth_ = width_; frameHeight_ = height_;
    }
    HDC buffer = frameDc_;
    const int saved = SaveDC(buffer);
    IntersectClipRect(buffer, paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right, paint.rcPaint.bottom);
    paintTitleBar(buffer);
    paintContent(buffer);
    paintStatusBar(buffer);

    RestoreDC(buffer, saved);
    BitBlt(dc, paint.rcPaint.left, paint.rcPaint.top,
           paint.rcPaint.right - paint.rcPaint.left, paint.rcPaint.bottom - paint.rcPaint.top,
           buffer, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);

    EndPaint(window_, &paint);

    // Frame timing. A wheel notch that takes longer than a few milliseconds is
    // felt as stutter, so slow frames are recorded rather than guessed at. The
    // log is throttled so a spinning wheel cannot flood it.
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
    if (elapsed >= kSlowFrameMilliseconds) {
        const ULONGLONG now = GetTickCount64();
        if (now - lastSlowFrameLog_ >= kSlowFrameLogIntervalMilliseconds) {
            lastSlowFrameLog_ = now;
            std::string entry = "slow frame: ";
            entry += std::to_string(elapsed);
            entry += " ms, tab=";
            entry += std::to_string(static_cast<int>(activeTab_));
            entry += ", dirty=";
            entry += std::to_string(paintedRegion_.right - paintedRegion_.left);
            entry += "x";
            entry += std::to_string(paintedRegion_.bottom - paintedRegion_.top);
            appendStartupLog(entry);
        }
    }
}

void MainWindow::paintTitleBar(HDC dc) {
    const RECT bar = titleBarRect();
    fillRect(dc, bar, theme::kTitleBar);

    RECT separator{0, bar.bottom - 1, width_, bar.bottom};
    fillRect(dc, separator, theme::kDivider);

    const auto header = ui::headerGeometry(width_, height_, tabWidths(), {static_cast<int>(dpi_)},0,visibleTabCount(),ui::visibleGpuTabs(nvidiaPresent_,radeonPresent_,overclockAllowed_));
    if (iconHeader_) DrawIconEx(dc, header.icon.left, header.icon.top, iconHeader_,
        header.icon.width(), header.icon.height(), 0, nullptr, DI_NORMAL);
    paintBrand(dc);
    paintTabs(dc);
    paintWindowButtons(dc);
}

void MainWindow::paintBrand(HDC dc) {
    const RECT rect = brandRect();
    if(rect.right<=rect.left)return;

    if (hotBrand_) {
        fillRect(dc, brandMenuRect(), theme::kHoverOverlay);
    }

    const auto geometry=ui::deviceBrandGeometry(fromNativeRect(rect),{static_cast<int>(dpi_)},
        brandManufacturerWidth_,brandModelWidth_,brandMetrics_,modelMetrics_);
    const UINT flags=DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS;
    drawTextIn(dc,brandManufacturer_.c_str(),toNativeRect(geometry.manufacturer),fontBrand_,theme::kTextPrimary,flags);
    drawTextIn(dc,brandModel_.c_str(),toNativeRect(geometry.model),fontStatus_,theme::kTextSecondary,flags);
    const int arrowCenterX=geometry.arrowX,arrowCenterY=geometry.arrowY;
    const int half = scale(4);

    // A filled downward triangle, matching the disclosure marker in the
    // reference.
    POINT triangle[3] = {
        {arrowCenterX - half, arrowCenterY - half / 2},
        {arrowCenterX + half, arrowCenterY - half / 2},
        {arrowCenterX, arrowCenterY + half},
    };
    HBRUSH brush = brushes_.get(theme::kTextSecondary);
    HGDIOBJ previousBrush = SelectObject(dc, brush);
    HGDIOBJ previousPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, triangle, 3);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
}
void MainWindow::showLanguageMenu() {
    const auto anchor=brandMenuRect();POINT point{anchor.left,anchor.bottom};ClientToScreen(window_,&point);
    const int choice=showLanguagePopup(window_,point,dpi_,selectedLanguage_);
    if(choice>=100&&choice<100+static_cast<int>(ui::kSupportedLaptops.size())){selectLaptop(choice-100);return;}
    if(choice<0||choice>=static_cast<int>(ui::kLanguageOptions.size()))return;
    auto next=preferences_;next.language=static_cast<unsigned>(choice);
    try{applyPreferences(next);}catch(const std::exception& e){appendStartupLog(e.what());return;}
    selectedLanguage_=choice;ui::setLanguage(next.language);applyWindowLanguage(window_);
    // A language can select a different private font even at unchanged DPI.
    onDpiChanged(static_cast<int>(dpi_));
    appendStartupLog(std::string("UI language: ")+ui::languageTag(ui::kLanguageOptions[selectedLanguage_].language));
    measureBrand();updatePageVisibility(!hiddenToTray_&&!IsIconic(window_));
    RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME|RDW_NOERASE);
}
void MainWindow::selectLaptop(int index){
    audit::input("windows.model_menu","model_selected","\"requested_index\":"+std::to_string(index)+",\"previous_index\":"+std::to_string(ui::selectedLaptopIndex()));
    audit::Scope trace("ui.model_preview","\"requested_index\":"+std::to_string(index),audit::capture());
    if(index<0||index>=static_cast<int>(ui::kSupportedLaptops.size())||index==ui::selectedLaptopIndex())return;
    if(deviceOperations().busy()){if(osd_)osd_->show({"device-profile","Ноутбук","Дождитесь завершения текущей операции.",osd::Icon::Information},window_);return;}
    const bool wasPreview=ui::laptopPreview(),preview=index!=ui::currentLaptop(readBoardDescriptor());
    if(osd_)osd_->hide(); // Discard old view notifications, not hardware work.
    updatePageVisibility(false);
    if(keysPage_)keysPage_->suspend();
    if(!wasPreview&&preview){realFansPage_=std::move(fansPage_);realSettingsPage_=std::move(settingsPage_);realKeysPage_=std::move(keysPage_);}
    else if(wasPreview){fansPage_.reset();settingsPage_.reset();keysPage_.reset();}
    ui::laptopSelection.store(index);audit::event("model_preview_selected","\"profile\":"+audit::quote(ui::kSupportedLaptops[index].id)+",\"preview\":"+(preview?"true":"false"));
    overclockAllowed_=overclock_&&!preview;
    if(preview&&activeTab_==Tab::OvClock)activeTab_=Tab::Fans;
    ui::footerStatuses().clear();
    if(ui::showRadeonTab(radeonPresent_)&&!radeonPage_)radeonPage_=std::make_unique<DiscreteGpuPage>(window_,GpuVendor::Radeon,osd_.get());
    if(!ui::showRadeonTab(radeonPresent_)&&activeTab_==Tab::Radeon)activeTab_=Tab::Fans;
    if(ui::showNvidiaTab(nvidiaPresent_)&&!nvidiaPage_)nvidiaPage_=std::make_unique<DiscreteGpuPage>(window_,GpuVendor::Nvidia,osd_.get());
    if(!ui::showNvidiaTab(nvidiaPresent_)&&activeTab_==Tab::Nvidia)activeTab_=Tab::Fans;
    if(preview){settingsPage_=std::make_unique<SettingsPage>(window_,nullptr,osd_.get(),appendStartupLog);
        keysPage_=std::make_unique<KeysPage>(window_,nullptr,preferences_,[this](const ApplicationPreferences& p){applyPreferences(p);},osd_.get());
        fansPage_=std::make_unique<FansPage>(window_,nullptr,osd_.get(),appendStartupLog);
    }else if(wasPreview){fansPage_=std::move(realFansPage_);settingsPage_=std::move(realSettingsPage_);keysPage_=std::move(realKeysPage_);}
    if(keysPage_)keysPage_->restore("profile_changed");
    const auto header=ui::deviceHeader(ui::presentationBoard());brandManufacturer_=mc::windows::encoding::wide(header.manufacturer);brandModel_=(preview?L"Просмотр · ":L"")+mc::windows::encoding::wide(header.model);measureBrand();
    const std::string detail=preview?"Просмотр профиля · аппаратное управление отключено":"Профиль текущего ноутбука · аппаратный доступ по проверке платы";
    appendStartupLog("Laptop selection: "+std::string(ui::kSupportedLaptops[index].name)+"; "+detail);
    updatePageVisibility(!hiddenToTray_&&!IsIconic(window_));repaint(window_);
    if(osd_)osd_->show({"device-profile",preview?"Просмотр ноутбука":"Ноутбук",std::string(ui::kSupportedLaptops[index].name),osd::Icon::Information},window_);
}
void MainWindow::paintLanguageItem(const DRAWITEMSTRUCT& item) {
    const auto index=static_cast<std::size_t>(item.itemID-kLanguageMenuFirst);
    fillRect(item.hDC,item.rcItem,(item.itemState&ODS_SELECTED)?theme::kHoverOverlay:theme::kSurface);
    RECT label=item.rcItem;label.left+=scale(28);label.right-=scale(12);
    const auto value=mc::windows::encoding::wide(std::string(ui::kLanguageOptions[index].label));
    drawTextIn(item.hDC,value.c_str(),label,fontStatus_,theme::kTextPrimary,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    if(item.itemState&ODS_CHECKED) {
        const int x=item.rcItem.left+scale(13),y=(item.rcItem.top+item.rcItem.bottom)/2,radius=scale(2);
        const auto brush=SelectObject(item.hDC,brushes_.get(theme::kTextSecondary));
        const auto pen=SelectObject(item.hDC,GetStockObject(NULL_PEN));
        Ellipse(item.hDC,x-radius,y-radius,x+radius,y+radius);SelectObject(item.hDC,pen);SelectObject(item.hDC,brush);
    }
}

void MainWindow::paintTabs(HDC dc) {
    for (int index = 0; index < visibleTabCount(); ++index) {
        const RECT rect = tabRect(index);
        if(IsRectEmpty(&rect))continue;
        const bool active = index == static_cast<int>(activeTab_);

        if (hotTab_ == index && !active) {
            fillRect(dc, rect, theme::kHoverOverlay);
        }

        drawTextIn(dc, kTabTitles[index], rect, active ? fontTabActive_ : fontTab_,
                   active ? theme::kTextPrimary : theme::kTextSecondary,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (active) {
            RECT underline{rect.left, rect.bottom - scale(kTabUnderlineHeight), rect.right,
                           rect.bottom};
            fillRect(dc, underline, theme::kAccent);
        }
    }
}

void MainWindow::paintWindowButtons(HDC dc) {

    const bool maximised = IsZoomed(window_) != 0;

    for (int index = kButtonMinimise; index <= kButtonClose; ++index) {
        const RECT rect = windowButtonRect(index);

        if (hotWindowButton_ == index) {
            fillRect(dc, rect, index == kButtonClose ? theme::kCloseHover : theme::kHoverOverlay);
        }

        const ColorRef color =
            (hotWindowButton_ == index) ? theme::kTextPrimary : theme::kTextSecondary;
        const int saved=SaveDC(dc),cx=(rect.left+rect.right)/2,cy=(rect.top+rect.bottom)/2,r=scale(5);
        const auto pen=CreatePen(PS_SOLID,std::max(1,scale(1)),color);SelectObject(dc,pen);SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        const auto stroke=[&](int a,int b,int x,int y){MoveToEx(dc,a,b,nullptr);LineTo(dc,x,y);};
        if(index==kButtonMinimise)stroke(cx-r,cy,cx+r,cy);
        else if(index==kButtonClose){stroke(cx-r,cy-r,cx+r,cy+r);stroke(cx+r,cy-r,cx-r,cy+r);}
        else if(maximised){Rectangle(dc,cx-r+scale(2),cy-r,cx+r,cy+r-scale(2));Rectangle(dc,cx-r,cy-r+scale(2),cx+r-scale(2),cy+r);}
        else Rectangle(dc,cx-r,cy-r,cx+r,cy+r);
        RestoreDC(dc,saved);DeleteObject(pen);
    }
}

void MainWindow::paintContent(HDC dc) {
    const RECT area = contentRect();

    fillRect(dc, area, theme::kBackground);

    switch (activeTab_) {
        case Tab::Fans:
            // The complete FANs tab is an independent child page.
            break;
        case Tab::Bios:
            break;
        case Tab::Sets:
            break;
        case Tab::Keys:
            break;
        case Tab::Nvidia:
        case Tab::Radeon:
            break;
        case Tab::OvClock:
            paintOverclock(dc,area);
            break;
    }
}

void MainWindow::refreshOverclock(){
    if(!overclock_)return;
    const HWND target=window_;
    overclock_->refresh([target]{PostMessageW(target,kOverclockReadyMessage,0,0);});
    const auto area=contentRect();InvalidateRect(window_,&area,FALSE);
}
void MainWindow::arrangeOverclock(bool show){
    const auto area=contentRect();overclockScroll_.setExtent(scale(440));overclockScroll_.setViewport(std::max(0,static_cast<int>(area.bottom-area.top)));
    const int x=area.right-scale(252),y=area.top+scale(51)-overclockScroll_.position(),w=scale(108),h=scale(30);
    const bool visible=show&&y>=area.top&&y+h<=area.bottom;
    if(overclockOff_){SendMessageW(overclockOff_,WM_SETFONT,reinterpret_cast<WPARAM>(fontStatus_),TRUE);MoveWindow(overclockOff_,x,y,w,h,TRUE);ShowWindow(overclockOff_,visible?SW_SHOWNA:SW_HIDE);}
    if(overclockOn_){SendMessageW(overclockOn_,WM_SETFONT,reinterpret_cast<WPARAM>(fontStatus_),TRUE);MoveWindow(overclockOn_,x+scale(112),y,w,h,TRUE);ShowWindow(overclockOn_,visible?SW_SHOWNA:SW_HIDE);}
}
void MainWindow::paintOverclock(HDC dc,const RECT& area){
    const int saved=SaveDC(dc);IntersectClipRect(dc,area.left,area.top,area.right,area.bottom);
    const int origin=area.top-overclockScroll_.position();
    const auto body=[&]{
    RECT card{area.left+scale(12),origin+scale(8),area.right-scale(12),origin+scale(440)};
    fillRect(dc,card,theme::kSurface);
    RECT title{area.left+scale(24),origin+scale(13),area.right-scale(24),origin+scale(45)};
    drawTextIn(dc,L"OvClock · WUJIE X5SP4NAG · UEFI / FwVars",title,fontTabActive_,theme::kTextPrimary,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT caption{area.left+scale(24),origin+scale(51),area.right-scale(270),origin+scale(81)};
    drawTextIn(dc,L"Память · только просмотр",caption,fontStatus_,theme::kTextSecondary,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(!overclock_||noEc_){RECT line{area.left+scale(24),origin+scale(90),area.right-scale(24),origin+scale(114)};
        drawTextIn(dc,L"Опрос недоступен",line,fontStatus_,theme::kTextMuted,DT_LEFT|DT_VCENTER|DT_SINGLELINE);return;}
    if(overclock_->busy()){RECT line{area.left+scale(24),origin+scale(90),area.right-scale(24),origin+scale(114)};
        drawTextIn(dc,L"Чтение…",line,fontStatus_,theme::kTextMuted,DT_LEFT|DT_VCENTER|DT_SINGLELINE);return;}
    auto rows=mc::overclockRows(overclock_->snapshot());int top=90;
    const int split=area.left+(area.right-area.left)*48/100;
    for(const auto& [key,value]:rows){
        RECT left{area.left+scale(24),origin+scale(top),split,origin+scale(top+24)};
        RECT right{split,origin+scale(top),area.right-scale(24),origin+scale(top+24)};
        const auto name=mc::windows::encoding::wide(key),text=mc::windows::encoding::wide(value);
        drawTextIn(dc,name.c_str(),left,fontStatus_,theme::kTextSecondary,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        drawTextIn(dc,text.c_str(),right,fontStatus_,theme::kTextPrimary,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        top+=26;
    }
    RECT footer{area.left+scale(24),origin+scale(408),area.right-scale(24),origin+scale(432)};
    drawTextIn(dc,L"Обновление при выборе вкладки · только чтение",footer,fontStatus_,theme::kTextMuted,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    };
    body();
    if(overclockScroll_.scrollable()){
        auto thumb=page::scrollThumb(area.right-area.left,area.bottom-area.top,overclockScroll_,dpi_,false,overclockDragging_);
        OffsetRect(&thumb,area.left,area.top);page::drawThumb(dc,thumb,false,overclockDragging_);
    }
    RestoreDC(dc,saved);
}

// ---------------------------------------------------------------------------
// BIOS page
// ---------------------------------------------------------------------------
// The page has two parts. A fixed band at the top holds the two action buttons,
// the two state lines and the two links; it never scrolls, matching the
// reference where the buttons stay reachable however long the inventory is.
// Below it a viewport scrolls the inventory cards.
//
// The inventory is rasterised once into an off-screen bitmap and then blitted at
// the scroll offset. Repainting a long page of text on every wheel notch was what
// made scrolling stutter, and it also let content bleed over the window chrome.
// ---------------------------------------------------------------------------

void MainWindow::paintStatusBar(HDC dc) {
    const RECT bar = statusBarRect();
    fillRect(dc, bar, theme::kTitleBar);

    RECT separator{0, bar.top, width_, bar.top + 1};
    fillRect(dc, separator, theme::kDivider);

    RECT text = bar;
    text.left += scale(kContentPadding);

    // The status text answers the two questions a user actually has: is the
    // hardware reachable, and is the value on screen live.
    std::wstring message;
    if (!sensorStatus_.empty()) {
        message = sensorStatus_;
    } else {
        message = fansPage_ ? fansPage_->statusText() : L"EC · ожидание опроса";
    }

    const std::wstring powerMarker = L"Потребление: ";
    std::wstring powerValue;
    const auto powerAt = message.rfind(powerMarker);
    if (sensorStatus_.empty() && powerAt != std::wstring::npos) {
        powerValue = message.substr(powerAt + powerMarker.size());
        message.resize(powerAt);
        const std::wstring statusSeparator = L" · ";
        if (message.size() >= statusSeparator.size() &&
            message.compare(message.size() - statusSeparator.size(), statusSeparator.size(), statusSeparator) == 0)
            message.resize(message.size() - statusSeparator.size());
    }

    const auto pageMessage=ui::footerStatuses().text(ui::footerSource(static_cast<int>(activeTab_)));
    if(ui::laptopPreview())message=L"Просмотр профиля · аппаратное управление отключено";
    else if(sensorStatus_.empty()&&!pageMessage.empty())message=mc::windows::encoding::wide(pageMessage);

    std::wstring label = L"MagicControl ";
    const std::string narrowVersion = MAGICCONTROL_VERSION_STRING;
    label.append(narrowVersion.begin(), narrowVersion.end());
    const auto previousFont = SelectObject(dc, fontStatus_);
    const auto textWidth = [&](const std::wstring& value) {
        SIZE size{};
        ::mc::gui::localizedTextExtent(dc, value.c_str(), static_cast<int>(value.size()), &size);
        return size.cx;
    };
    const int versionWidth = textWidth(label);
    const int powerLabelWidth = textWidth(powerMarker);
    const int powerValueWidth = textWidth(powerValue);
    SelectObject(dc, previousFont);

    RECT version = bar;
    version.right -= scale(kContentPadding);
    version.left = version.right - versionWidth;
    versionRect_ = version;
    if (!powerValue.empty()) {
        RECT powerRect = bar;
        powerRect.right = version.left - scale(20);
        powerRect.left = powerRect.right - powerLabelWidth - powerValueWidth;
        text.right = powerRect.left - scale(16);
        drawTextIn(dc, powerMarker.c_str(), powerRect, fontStatus_, theme::kTextMuted,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        powerRect.left += powerLabelWidth;
        drawTextIn(dc, powerValue.c_str(), powerRect, fontStatus_, theme::kError,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else text.right = version.left - scale(16);
    drawTextIn(dc, message.c_str(), text, fontStatus_,
               sensorStatus_.empty() || noEc_ ? theme::kTextMuted : theme::kError,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    // Match the logo: violet -> blue -> turquoise, one colour per character.
    const auto mixColour = [](COLORREF a, COLORREF b, double fraction) {
        const auto mix = [fraction](int first, int last) {
            return static_cast<int>(first + (last - first) * fraction + 0.5);
        };
        return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)),
                   mix(GetBValue(a), GetBValue(b)));
    };
    const auto gradientFont = SelectObject(dc, fontStatus_);
    std::vector<int> characterOffsets(label.size());
    for (std::size_t i = 0; i < label.size(); ++i) {
        SIZE prefixSize{};
        ::mc::gui::localizedTextExtent(dc, label.c_str(), static_cast<int>(i), &prefixSize);
        characterOffsets[i] = prefixSize.cx;
    }
    SelectObject(dc, gradientFont);
    for (std::size_t i = 0; i < label.size(); ++i) {
        const double fraction = label.size() > 1 ? static_cast<double>(i) / (label.size() - 1) : 0;
        const COLORREF colour = fraction < 0.5
            ? mixColour(RGB(160, 50, 255), RGB(65, 110, 255), fraction * 2)
            : mixColour(RGB(65, 110, 255), RGB(25, 235, 235), (fraction - 0.5) * 2);
        RECT characterRect = version;
        characterRect.left += characterOffsets[i];
        const wchar_t character[]{label[i], 0};
        drawTextIn(dc, character, characterRect, fontStatus_, colour,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

}

}  // namespace mc::gui

