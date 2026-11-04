find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.12)
pkg_check_modules(PANGO_FT2 REQUIRED IMPORTED_TARGET pangoft2)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(LAYER_SHELL QUIET IMPORTED_TARGET gtk4-layer-shell-0)
find_program(MAGICCONTROL_GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(MAGICCONTROL_ICON_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/magiccontrol-icon-resources.cpp")
add_custom_command(OUTPUT "${MAGICCONTROL_ICON_RESOURCES}"
    COMMAND "${MAGICCONTROL_GLIB_COMPILE_RESOURCES}"
        --generate-source --c-name=magiccontrol_icons
        "--target=${MAGICCONTROL_ICON_RESOURCES}"
        "--sourcedir=${PROJECT_SOURCE_DIR}/src/gui/assets"
        "${PROJECT_SOURCE_DIR}/src/gui/assets/magiccontrol-icons.gresource.xml"
    DEPENDS "${PROJECT_SOURCE_DIR}/src/gui/assets/magiccontrol-icons.gresource.xml"
            "${PROJECT_SOURCE_DIR}/src/gui/assets/magiccontrol-512.png"
            "${PROJECT_SOURCE_DIR}/src/gui/assets/donation-wallet.png"
            "${PROJECT_SOURCE_DIR}/GitHub_Releases/LICENSE.txt"
    VERBATIM)

add_executable(magiccontrol-gui
    "${MAGICCONTROL_ICON_RESOURCES}"
    main.cpp
    NumericField.cpp
    Ui.cpp
    MainWindow.cpp
    AboutPanel.cpp
    UpdateWindow.cpp
    FansPage.cpp
    SettingsPage.cpp
    SettingsSleepMonitor.cpp
    KeysPage.cpp
    BiosPage.cpp
    OvClockPage.cpp
    NvidiaGpuPage.cpp
    OsdService.cpp
    TrayIcon.cpp
)

target_link_libraries(magiccontrol-gui
    PRIVATE
        magiccontrol_core
        magiccontrol_ui_config
        magiccontrol_platform
        magiccontrol_update
        PkgConfig::GTK4
        PkgConfig::PANGO_FT2
        PkgConfig::FONTCONFIG
)

target_compile_features(magiccontrol-gui PRIVATE cxx_std_20)
set_target_properties(magiccontrol-gui PROPERTIES BUILD_RPATH "$ORIGIN")
if(LAYER_SHELL_FOUND)
    target_link_libraries(magiccontrol-gui PRIVATE PkgConfig::LAYER_SHELL)
    target_compile_definitions(magiccontrol-gui PRIVATE MAGICCONTROL_LAYER_SHELL=1)
    find_library(MAGICCONTROL_LAYER_SHELL_LIBRARY NAMES gtk4-layer-shell HINTS ${LAYER_SHELL_LIBRARY_DIRS} REQUIRED)
    set_property(TARGET magiccontrol-gui PROPERTY MAGICCONTROL_OSD_LAYER_RUNTIME "${MAGICCONTROL_LAYER_SHELL_LIBRARY}")
endif()

set_target_properties(magiccontrol-gui PROPERTIES
    OUTPUT_NAME MagicControl
    FOLDER "MagicControl"
)

message(STATUS "  gui           : MagicControl (GTK4 / Wayland)")

if(MAGICCONTROL_BUILD_TESTS)
    add_executable(magiccontrol_gtk_settings_tests
        ${PROJECT_SOURCE_DIR}/tests/test_gtk_settings.cpp SettingsPage.cpp SettingsSleepMonitor.cpp OsdService.cpp Ui.cpp NumericField.cpp)
    target_compile_definitions(magiccontrol_gtk_settings_tests PRIVATE MAGICCONTROL_GTK_SETTINGS_TEST)
    target_link_libraries(magiccontrol_gtk_settings_tests PRIVATE magiccontrol_core magiccontrol_platform
        magiccontrol_ui_config PkgConfig::GTK4 PkgConfig::PANGO_FT2 PkgConfig::FONTCONFIG)
    add_test(NAME magiccontrol_gtk_settings_tests COMMAND magiccontrol_gtk_settings_tests)
    set_tests_properties(magiccontrol_gtk_settings_tests PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "GSK_RENDERER=cairo")
    add_executable(magiccontrol_gtk_fans_tests
        ${PROJECT_SOURCE_DIR}/tests/test_gtk_fans.cpp FansPage.cpp OsdService.cpp Ui.cpp NumericField.cpp)
    target_link_libraries(magiccontrol_gtk_fans_tests PRIVATE "$<LINK_GROUP:RESCAN,magiccontrol_core,magiccontrol_platform>"
        magiccontrol_ui_config PkgConfig::GTK4 PkgConfig::PANGO_FT2 PkgConfig::FONTCONFIG)
    add_test(NAME magiccontrol_gtk_fans_tests COMMAND magiccontrol_gtk_fans_tests)
    set_tests_properties(magiccontrol_gtk_fans_tests PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "GSK_RENDERER=cairo")
    add_executable(magiccontrol_gtk_fans_resource_tests
        ${PROJECT_SOURCE_DIR}/tests/test_gtk_fans_resource.cpp ${PROJECT_SOURCE_DIR}/tests/test_board_fixture.cpp
        FansPage.cpp OsdService.cpp Ui.cpp NumericField.cpp)
    target_link_libraries(magiccontrol_gtk_fans_resource_tests PRIVATE "$<LINK_GROUP:RESCAN,magiccontrol_core,magiccontrol_platform>"
        magiccontrol_ui_config PkgConfig::GTK4 PkgConfig::PANGO_FT2 PkgConfig::FONTCONFIG)
    add_test(NAME magiccontrol_gtk_fans_resource_state COMMAND magiccontrol_gtk_fans_resource_tests enabled)
    add_test(NAME magiccontrol_gtk_fans_resource_legacy_env_ignored COMMAND magiccontrol_gtk_fans_resource_tests disabled)
    set_tests_properties(magiccontrol_gtk_fans_resource_state PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "GSK_RENDERER=cairo;MAGICCONTROL_RESOURCE_UI=1")
    set_tests_properties(magiccontrol_gtk_fans_resource_legacy_env_ignored PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "GSK_RENDERER=cairo;MAGICCONTROL_RESOURCE_UI=0")
endif()
