// MagicControl - Windows GUI entry point.
//
// The process is per-monitor DPI aware, declared in the application manifest.
// That makes the window crisp on high-DPI panels and lets WM_DPICHANGED relayout
// when the window moves between monitors with different scaling.

#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>

#include <cstdlib>
#include <string>

#include "Layout.h"
#include "MainWindow.h"
#include "MessageDialog.h"
#include "../../platform/windows/TextWindows.h"
#include "LocalizationWindows.h"
#include "InstanceHandoff.h"
#include "MagicControl/Logging.h"
#include "MagicControl/RomIdHistory.h"
#include "MagicControl/AppUpdate.h"
#include "MagicControl/BiosRomDump.h"
#include "MagicControl/PortableSettings.h"
#include "../../platform/windows/BiosRomDumpWindows.h"
#include "../../platform/windows/ApplicationPreferencesWindows.h"
#include "../../platform/windows/DiagnosticWmiWindows.h"
#include "../../platform/common/DiagnosticDumpInternal.h"

namespace {

/// Reads "--tab=<name>" from the command line.
///
/// The switch exists so a screenshot or a debugging session can open directly on
/// one page. It also removes the need for a click to reach a page when the
/// interface is being reviewed.
mc::gui::Tab parseStartTab() {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return mc::gui::Tab::Fans;
    }

    mc::gui::Tab tab = mc::gui::Tab::Fans;
    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring argument = arguments[index];
        if (argument.rfind(L"--tab=", 0) != 0) {
            continue;
        }
        const std::wstring value = argument.substr(6);
        if (value == L"fans") {
            tab = mc::gui::Tab::Fans;
        } else if (value == L"sets") {
            tab = mc::gui::Tab::Sets;
        } else if (value == L"keys") {
            tab = mc::gui::Tab::Keys;
        } else if (value == L"bios" || value == L"info") {
            tab = mc::gui::Tab::Bios;
        } else if (value == L"nvidia") {
            tab = mc::gui::Tab::Nvidia;
        }
    }

    LocalFree(arguments);
    return tab;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*previousInstance*/, PWSTR /*commandLine*/,
                    int showCommand) {
    int count{};auto** args=CommandLineToArgvW(GetCommandLineW(),&count);
    std::vector<std::string> values;
    if(args){for(int i=0;i<count;++i)values.push_back(mc::windows::encoding::utf8(args[i]));LocalFree(args);}
    if(auto result=mc::update::runHelper(values))return *result;
    if(auto result=mc::windows::wmi::runDiagnosticHelper(values))return *result;
    if(auto result=mc::diagnostics::runFastDiagnosticHelper(values))return *result;
    // Explicit console-free diagnostic entry; same reader as the INFO button.
    // Runs before MainWindow, EC owners, startup restore or instance handoff.
    if(auto result=mc::windows::runBiosDumpHelper(values))return *result;
    // Startup failures can precede MainWindow::create(), which normally loads
    // the saved UI language. Read it here without creating or rewriting settings.
    mc::ApplicationPreferences preferences;std::string preferencesError;
    if(mc::windows::readStoredApplicationPreferences(preferences,preferencesError))
        mc::gui::ui::setLanguage(preferences.language);
    // Bootstrap dialogs must use the same private collection as the main UI.
    if(!mc::gui::registerBundledUiFonts(mc::portableSettingsPath("fonts"))){
        OutputDebugStringW(L"MagicControl: bundled fonts missing/invalid; refusing system-font fallback.\n");return EXIT_FAILURE;}
    try { winrt::init_apartment(winrt::apartment_type::single_threaded); }
    catch (const winrt::hresult_error& error) {
        mc::gui::showInformationDialog(nullptr,96,error.message().c_str());
        return EXIT_FAILURE;
    }
    struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
    // Inspect local executable versions before creating any hardware owner.
    // Lease lifetime extends past MainWindow destruction and worker cleanup.
    mc::gui::InstanceLease lease;
    const auto admission=lease.acquire(preferences.allLogs);
    if(admission==mc::gui::InstanceLease::Result::ActivatedExisting){
        mc::recordStartupRomId(wcsstr(GetCommandLineW(),L"--no-ec")==nullptr);
        return EXIT_SUCCESS;
    }
    if(admission==mc::gui::InstanceLease::Result::Failed){
        mc::gui::showInformationDialog(nullptr,96,mc::gui::localized(lease.error.c_str()));return EXIT_FAILURE;
    }
    try{lease.migrateSettings();}
    catch(const std::exception& error){
        mc::gui::showInformationDialog(nullptr,96,mc::windows::encoding::wide(error.what()).c_str());return EXIT_FAILURE;
    }

    mc::recordStartupRomId(wcsstr(GetCommandLineW(),L"--no-ec")==nullptr);

    mc::gui::MainWindow window;
    if (!window.create(instance, showCommand, parseStartTab(),
                       wcsstr(GetCommandLineW(), L"--no-ec") != nullptr)) {
        mc::gui::showInformationDialog(nullptr,96,mc::gui::localized(L"Не удалось создать окно."));
        return EXIT_FAILURE;
    }
    lease.releaseLaunchGate();
    mc::logMessage("application","Instance admission: "+lease.summary);
    return window.runMessageLoop();
}
