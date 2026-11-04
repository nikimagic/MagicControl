// MagicControl - Windows graphical interface.
//
// Win32. The Visual C++ runtime is linked into the portable executable.
//
// The window is frameless. WM_NCCALCSIZE removes the standard non-client area,
// which lets the custom title bar own the whole top strip, while DWM still
// provides the drop shadow and the resize borders stay functional through
// WM_NCHITTEST.
#pragma once

#include <windows.h>

#include <array>
#include <mutex>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>

#include "MagicControl/EcAccess.h"
#include "MagicControl/PortableSettings.h"
#include "MagicControl/Overclock.h"
#include "MagicControl/ScrollModel.h"
#include "Layout.h"
#include "../common/Theme.h"
#include "BiosPage.h"
#include "SettingsPage.h"
#include "KeysPage.h"
#include "FansPage.h"
#include "DiscreteGpuPage.h"
#include "../../platform/windows/OemEventMonitor.h"
#include "../../platform/windows/OsdSystemMonitor.h"
#include "OsdPopup.h"
#include "../../platform/windows/PowerDiagnosticsWindows.h"
#include "../../platform/windows/ConfigurationRestoreWindows.h"

namespace mc::windows {class Gm6Controller;}
namespace mc {class DisplayRotationOperations;}
namespace mc::gui {

/// Owns the application window and all GDI resources it draws with.
class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    /// Registers the window class, creates the window and shows it.
    /// `startTab` selects the page shown first, which lets a screenshot or a
    /// debugging run open directly on one page.
    bool create(HINSTANCE instance, int showCommand, Tab startTab = Tab::Fans, bool noEc = false);

    /// Runs the message loop until the window closes.
    int runMessageLoop();

private:
    std::optional<DWORD> acPowerSource_;
    ULONGLONG adapterNoticeAfter_{};
    std::optional<DWORD> sessionDisplayState_;
    void restoreConfiguration(const std::string& reason);
    void restoreKeysApplicationFlags();
    // --- Window plumbing ---------------------------------------------------
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    std::wstring pawnIoStartupMessage_;
    void onCreate();
    void onDestroy();
    void requestExit();
    void updatePageVisibility(bool visible);
    void selectLaptop(int index);
    void applyApplicationIcons();
    bool addTrayIcon();
    void removeTrayIcon();
    void hideToTray();
    void openFromTray();
    void showTrayMenu(POINT anchor);
    void onPaint();
    void onDpiChanged(int newDpi);
    void onMouseMove(int x, int y);
    void onMouseLeave();
    void onLeftButtonDown(int x, int y);
    void onLeftButtonUp(int x, int y);

    /// Handles WM_NCHITTEST so the frameless window can still be dragged and
    /// resized like a normal one.
    [[nodiscard]] LRESULT onNcHitTest(POINT screenPoint) const;

    /// Handles WM_GETMINMAXINFO so a maximised window fills the work area
    /// exactly, without covering the taskbar.
    void onGetMinMaxInfo(MINMAXINFO* info) const;

    // --- Geometry ----------------------------------------------------------
    [[nodiscard]] int scale(int logicalPixels) const noexcept;
    [[nodiscard]] RECT titleBarRect() const noexcept;
    [[nodiscard]] RECT brandRect() const noexcept;
    [[nodiscard]] RECT brandMenuRect() const noexcept;
    [[nodiscard]] RECT tabRect(int index) const noexcept;
    [[nodiscard]] std::array<int,kTabCount> tabWidths() const noexcept;
    [[nodiscard]] int visibleTabCount() const noexcept;
    [[nodiscard]] RECT windowButtonRect(int index) const noexcept;
    [[nodiscard]] RECT contentRect() const noexcept;
    [[nodiscard]] RECT statusBarRect() const noexcept;

    /// Which tab, if any, sits under a client-area point. Returns -1 for none.
    [[nodiscard]] int tabAt(int x, int y) const noexcept;
    /// Which window button, if any, sits under a client-area point.
    /// 0 = minimise, 1 = maximise, 2 = close, -1 = none.
    [[nodiscard]] int windowButtonAt(int x, int y) const noexcept;
    [[nodiscard]] bool brandContains(int x, int y) const noexcept;

    // --- Painting ----------------------------------------------------------
    void createFonts();
    void measureBrand();
    void destroyFonts();

    void paintTitleBar(HDC dc);
    void paintBrand(HDC dc);
    void showLanguageMenu();
    void paintLanguageItem(const DRAWITEMSTRUCT& item);
    void paintTabs(HDC dc);
    void paintWindowButtons(HDC dc);
    void paintContent(HDC dc);
    void paintOverclock(HDC dc,const RECT& area);
    void refreshOverclock();
    void arrangeOverclock(bool show);
    void paintStatusBar(HDC dc);

    void fillRect(HDC dc, const RECT& rect, ColorRef color) const;

    void drawTextIn(HDC dc, const wchar_t* text, const RECT& rect, HFONT font, ColorRef color,
                    UINT format) const;

    void handleOemEvent(std::uint64_t code, std::uint64_t receivedAt);
    void startScreenFlip(std::uint64_t receivedAt);
    void completeScreenFlip();
    void applyPreferences(const ApplicationPreferences& preferences);

    // --- State -------------------------------------------------------------
    HWND window_{nullptr};
    HWND overclockOff_{},overclockOn_{};
    bool overclockAllowed_{};
    std::unique_ptr<mc::OverclockOperations> overclock_;
    mc::ScrollModel overclockScroll_;bool overclockDragging_{};int overclockGrabOffset_{};
    HWND trayMenuOwner_{};
    HICON iconLarge_{}, iconSmall_{}, iconHeader_{};
    UINT taskbarCreated_{};
    bool trayAdded_{}, hiddenToTray_{}, trayRestoreMaximized_{}, exiting_{};
    bool trayMenuTracking_{};

    /// Region BeginPaint reported for the current frame, used by the frame timer.
    RECT paintedRegion_{};
    RECT versionRect_{}; // Measured footer hit area; never changes page/card geometry.
    ULONGLONG lastSlowFrameLog_{0};
    HINSTANCE instance_{nullptr};
    UINT dpi_{96};
    int width_{0};
    int height_{0};

    /// The embedded controller session. Owned for the lifetime of the window.
    /// Null when the driver could not be opened, in which case the interface
    /// still runs and shows dashes rather than failing to start.
    std::shared_ptr<EcAccess> ec_;

    /// Description of why sensor data is unavailable, empty when it works.
    std::wstring sensorStatus_;

    std::unique_ptr<BiosPage> biosPage_;
    std::unique_ptr<SettingsPage> settingsPage_;
    std::unique_ptr<KeysPage> keysPage_;
    std::unique_ptr<FansPage> fansPage_;
    // Keep live pages/controllers while a different model is being previewed.
    std::unique_ptr<FansPage> realFansPage_;
    std::unique_ptr<SettingsPage> realSettingsPage_;
    std::unique_ptr<KeysPage> realKeysPage_;
    std::unique_ptr<DiscreteGpuPage> nvidiaPage_;
    bool nvidiaPresent_{},radeonPresent_{};
    std::unique_ptr<DiscreteGpuPage> radeonPage_;
    std::shared_ptr<mc::windows::FanController> fanController_;
    std::unique_ptr<mc::windows::ConfigurationRestore> configurationRestore_;
    std::unique_ptr<mc::DisplayRotationOperations> screenFlip_;
    std::unique_ptr<mc::windows::OemEventMonitor> oemEvents_;
    struct NativeProfileInbox;
    std::shared_ptr<NativeProfileInbox> nativeProfileInbox_;
    std::shared_ptr<mc::windows::Gm6Controller> nativeProfileController_;
    void queueNativeProfileCycle(std::uint64_t receivedAt);
    void finishNativeProfileCommands();
    void closeNativeProfileCommands();
    std::unique_ptr<OsdPopup> osd_;
    std::unique_ptr<mc::windows::OsdSystemMonitor> systemEvents_;
    std::unique_ptr<mc::windows::PowerDiagnostics> powerDiagnostics_;
    std::mutex cameraOsdGate_;
    std::uint64_t cameraOsdToken_{}, cameraOsdAt_{};
    ApplicationPreferences preferences_;
    bool noEc_{false};
    HDC frameDc_{};
    HBITMAP frameBitmap_{};
    HGDIOBJ framePrevious_{};
    int frameWidth_{}, frameHeight_{};
    void clearFrameBuffer();

    Tab activeTab_{Tab::Fans};
    int hotTab_{-1};           ///< Tab under the cursor, -1 for none.
    int hotWindowButton_{-1};  ///< Window button under the cursor, -1 for none.
    bool hotBrand_{false};
    bool trackingMouse_{false};
    std::wstring brandManufacturer_{L"MagicControl"}, brandModel_;
    int brandManufacturerWidth_{}, brandModelWidth_{};
    ui::TextMetrics brandMetrics_{}, modelMetrics_{};
    int selectedLanguage_{};

    // Fonts, created once per DPI.
    HFONT fontBrand_{nullptr};
    HFONT fontTab_{nullptr};
    HFONT fontTabActive_{nullptr};
    HFONT fontStatus_{nullptr};

    /// Solid brushes, one per palette colour actually used during painting.
    /// Cached because a repaint happens on every mouse move over the chrome.
    struct BrushCache {
        std::array<std::pair<ColorRef, HBRUSH>, 16> entries{};
        std::size_t count{0};

        [[nodiscard]] HBRUSH get(ColorRef color);
        void clear();
    };
    BrushCache brushes_;
};

}  // namespace mc::gui
