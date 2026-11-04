# ---------------------------------------------------------------------------
# MagicControl Windows GUI.
#
# Win32 GUI with a static MSVC runtime; all reviewed driver/runtime payloads are embedded.
# ---------------------------------------------------------------------------
configure_file(MagicControl.rc.in "${CMAKE_CURRENT_BINARY_DIR}/MagicControl.rc" @ONLY)

add_executable(magiccontrol-gui WIN32
    "${CMAKE_CURRENT_BINARY_DIR}/MagicControl.rc"
    main.cpp
    InstanceHandoff.cpp
    MainWindow.cpp
    LanguagePopup.cpp
    UpdateWindow.cpp
    MessageDialog.cpp
    BiosPage.cpp
    SettingsPage.cpp
    KeysPage.cpp
    FansPage.cpp
    NvidiaGpuPage.cpp
    NumericFieldTheme.cpp
    OsdPopup.cpp
    MagicControl.exe.manifest
)

target_link_libraries(magiccontrol-gui
    PRIVATE
        magiccontrol_core
        magiccontrol_ui_config
        magiccontrol_platform
        magiccontrol_update
        # DWM composition: drop shadow, dark frame and rounded-corner preference.
        dwmapi
        # Per-monitor DPI helpers.
        shcore
        # Shell_NotifyIcon and the common dialogs, used by the tray and later pages.
        shell32
        comctl32
        comdlg32
        gdi32
        user32
        version
        ole32
        windowscodecs
)

# The manifest is embedded rather than emitted beside the executable, so DPI
# awareness and the supported-OS list travel with the binary.
target_link_options(magiccontrol-gui PRIVATE
    "/MANIFEST:EMBED"
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/MagicControl.exe.manifest"
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
)

# A windowed program: no console window appears behind the interface.
set_target_properties(magiccontrol-gui PROPERTIES
    OUTPUT_NAME "MagicControl"
    FOLDER "MagicControl"
    VS_DPI_AWARE "PerMonitor"
)

if(MSVC)
    target_compile_options(magiccontrol-gui PRIVATE /W4)
    # UNICODE entry point: wWinMain.
    target_link_options(magiccontrol-gui PRIVATE "/ENTRY:wWinMainCRTStartup")
endif()

message(STATUS "  gui           : MagicControl.exe (win32)")

magiccontrol_sign_target(magiccontrol-gui)

magiccontrol_embed_runtime(magiccontrol-gui)
set(MAGICCONTROL_PAWNIO_RUNTIME "${PROJECT_SOURCE_DIR}/src/platform/windows/runtime/PawnIO")

set(MAGICCONTROL_PORTABLE_WINDOWS_DIR "${PROJECT_SOURCE_DIR}/MagicControl/windows")
set(_cli_stage_args)
set(_cli_stage_dependencies)
if(TARGET magiccontrol)
    list(APPEND _cli_stage_args "-DCLI_EXECUTABLE=$<TARGET_FILE:magiccontrol>")
    list(APPEND _cli_stage_dependencies magiccontrol)
endif()
# Keep packaging a separate ALL target: even an otherwise up-to-date build
# repairs a deleted portable executable. User settings are untouched.
add_custom_target(magiccontrol-portable ALL
    COMMAND ${CMAKE_COMMAND}
            "-DCONFIG=$<CONFIG>"
            "-DEXECUTABLE=$<TARGET_FILE:magiccontrol-gui>"
            ${_cli_stage_args}
            "-DDESTINATION=${MAGICCONTROL_PORTABLE_WINDOWS_DIR}"
            "-DSTATIC_CRT=TRUE"
            "-DPAWNIO_RUNTIME=${MAGICCONTROL_PAWNIO_RUNTIME}"
            "-DVERSION_FILE=${CMAKE_BINARY_DIR}/generated/MagicControl/Version.h.version"
            -P "${PROJECT_SOURCE_DIR}/cmake/StagePortableWindows.cmake"
    DEPENDS magiccontrol-gui ${_cli_stage_dependencies}
    VERBATIM)
