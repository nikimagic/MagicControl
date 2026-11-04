# Legacy filename retained for existing generated build graphs. Native builds
# now CONSUME a product version; only Tools/Build-All.ps1 reserves a new number.
if(NOT EXISTS "${COUNTER_DIRECTORY}/product-version.txt")
    message(FATAL_ERROR "No product version selected. Run Tools/Build-All.ps1 first.")
endif()
file(READ "${COUNTER_DIRECTORY}/product-version.txt" PROJECT_VERSION)
string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)
if(NOT PROJECT_VERSION MATCHES "^${PROJECT_VERSION_MAJOR}\\.${PROJECT_VERSION_MINOR}\\.([0-9]+)$")
    message(FATAL_ERROR "Invalid MagicControl product version: ${PROJECT_VERSION}")
endif()
set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_1}")
configure_file("${VERSION_TEMPLATE}" "${VERSION_HEADER}" @ONLY)
file(WRITE "${VERSION_HEADER}.version" "${PROJECT_VERSION}\n")
message(STATUS "MagicControl product version: ${PROJECT_VERSION} (shared by Windows/Linux)")
