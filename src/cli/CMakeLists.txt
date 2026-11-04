# ---------------------------------------------------------------------------
# MagicControl command line tool.
#
# SSH adapters to the current guarded core. No raw EC writers or startup restore.
# Actual OS/model capabilities stay in the same backends used by GUI.
#
# The output name is mctl, NOT magiccontrol: the GUI produces MagicControl.exe,
# and on Windows, whose file system ignores case, two targets named
# MagicControl.exe and magiccontrol.exe would be the same file and would
# overwrite each other on every build. Keep these names distinct.
# ---------------------------------------------------------------------------
add_executable(magiccontrol
    main.cpp
    CurrentCommands.cpp
)

target_link_libraries(magiccontrol
    PRIVATE
        magiccontrol_core
        magiccontrol_platform
)

set_target_properties(magiccontrol PROPERTIES
    OUTPUT_NAME "mctl"
    FOLDER "MagicControl"
)

if(MSVC)
    # The CLI is a console program even though the GUI is a windowed one.
    target_compile_options(magiccontrol PRIVATE /W4)
    configure_file(Version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Version.rc" @ONLY)
    target_sources(magiccontrol PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Version.rc")
    target_link_options(magiccontrol PRIVATE "/MANIFEST:EMBED" "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
endif()

if(UNIX)
    # Only existing Linux adapters; no Windows EC protocol emulation.
    target_compile_definitions(magiccontrol PRIVATE MC_PLATFORM_LINUX=1)
    target_link_options(magiccontrol PRIVATE -static-libstdc++ -static-libgcc)
endif()

magiccontrol_sign_target(magiccontrol)

if(WIN32)
    magiccontrol_embed_runtime(magiccontrol)
    message(STATUS "  cli           : mctl.exe")
else()
    message(STATUS "  cli           : mctl")
endif()
