# =============================================================================
# CMakeLists.txt — MediaX Build System
# Build system authored by Ankit Kimothi
#
# MediaX is a college C++ project that wraps an HTML/CSS/JS media player
# UI inside a native Windows executable using Microsoft WebView2.
#
# HOW TO BUILD (Windows):
#   1. Install CMake >= 3.20 and a C++17-capable compiler (MSVC recommended).
#   2. Download the WebView2 SDK:
#        Option A: NuGet — search for "Microsoft.Web.WebView2" in Visual Studio's
#                  NuGet Package Manager, install to this project.
#        Option B: GitHub — https://github.com/MicrosoftEdge/WebView2Samples
#        Either way, place (or symlink) the extracted SDK so that the following
#        path exists:
#          vendor/webview2/build/native/include/WebView2.h
#          vendor/webview2/build/native/x64/WebView2LoaderStatic.lib  (for static)
#        Adjust WEBVIEW2_INCLUDE_DIR and WEBVIEW2_LIB below if your layout differs.
#   3. mkdir build && cd build
#      cmake .. -G "Visual Studio 17 2022" -A x64
#      cmake --build . --config Release
#   4. The resulting MediaX.exe and the copied resources/ folder will be in
#      build/Release/ (or build/ for single-config generators).
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(MediaX VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Collect sources automatically from bridge/ and engine/ subdirectories.
# Any new .cpp file dropped into those folders will be picked up at next
# CMake configure without editing this file.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE BRIDGE_SOURCES  CONFIGURE_DEPENDS "bridge/*.cpp")
file(GLOB_RECURSE ENGINE_SOURCES  CONFIGURE_DEPENDS "engine/*.cpp")

set(MEDIAX_SOURCES
    main.cpp
    ${BRIDGE_SOURCES}
    ${ENGINE_SOURCES}
)

# ---------------------------------------------------------------------------
# WIN32 executable — suppresses the console window on Windows so only the
# WebView2 window is shown to the user.  On other platforms (dev/CI only)
# a plain executable is produced so the stub main() can be tested.
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(MediaX WIN32 ${MEDIAX_SOURCES})
else()
    add_executable(MediaX ${MEDIAX_SOURCES})
endif()

# Also build the C++ verification test console (useful for viva / testing)
add_executable(verify_backend test_runner.cpp ${BRIDGE_SOURCES} ${ENGINE_SOURCES})
target_include_directories(verify_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
target_include_directories(MediaX PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # so #include "bridge/Bridge.h" etc. work
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

# ---------------------------------------------------------------------------
# WebView2 SDK — set WEBVIEW2_DIR via cmake -DWEBVIEW2_DIR=<path> or set a
# WEBVIEW2_DIR environment variable, or place the SDK at vendor/webview2/.
# If not found, the build will still succeed but WebView2-dependent code
# must be guarded with #ifdef HAVE_WEBVIEW2.
# ---------------------------------------------------------------------------
set(WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/webview2"
    CACHE PATH "Root of the Microsoft WebView2 SDK (contains build/native/)")

set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_DIR}/build/native/include")
set(WEBVIEW2_LIB         "${WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib")

if(EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
    message(STATUS "WebView2 SDK found at: ${WEBVIEW2_DIR}")
    target_include_directories(MediaX PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
    target_link_libraries(MediaX PRIVATE "${WEBVIEW2_LIB}")
    target_compile_definitions(MediaX PRIVATE HAVE_WEBVIEW2=1)
else()
    message(WARNING
        "WebView2 SDK not found at '${WEBVIEW2_DIR}'.\n"
        "Download it from NuGet (Microsoft.Web.WebView2) or\n"
        "https://github.com/MicrosoftEdge/WebView2Samples and place it at:\n"
        "  ${WEBVIEW2_DIR}\n"
        "The project will compile without WebView2 support.")
endif()

# ---------------------------------------------------------------------------
# Platform-specific system libraries
# ---------------------------------------------------------------------------
if(WIN32)
    # ole32    — COM initialisation (CoInitializeEx)
    # shell32  — ShellExecute, path utilities
    # user32   — Win32 window management (CreateWindowEx, GetMessage …)
    # combase  — Modern COM (RoInitialize for WinRT on older toolchains)
    target_link_libraries(MediaX PRIVATE
        ole32
        shell32
        user32
        combase
    )
endif()

# ---------------------------------------------------------------------------
# Compiler warnings — treat warnings as errors in Debug builds to keep code
# quality high for the college submission.
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(MediaX PRIVATE
        /W4             # High warning level
        $<$<CONFIG:Debug>:/WX>   # Warnings-as-errors in Debug only
    )
else()
    target_compile_options(MediaX PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-Werror>
    )
endif()

# ---------------------------------------------------------------------------
# Post-build: copy the resources/ folder (web assets: index.html, styles.css,
# app.js, icons …) next to the compiled executable so the app can load them
# with a relative path at runtime.
# ---------------------------------------------------------------------------
add_custom_command(TARGET MediaX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:MediaX>/resources"
    COMMENT "Copying web resources next to MediaX executable"
)

# ---------------------------------------------------------------------------
# Install rules (optional — useful for packaging the final submission)
# ---------------------------------------------------------------------------
install(TARGETS MediaX DESTINATION bin)
install(DIRECTORY resources/ DESTINATION bin/resources)

# ---------------------------------------------------------------------------
# Print a helpful summary after configuration
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "===== MediaX Configuration Summary =====")
message(STATUS "  Version      : ${PROJECT_VERSION}")
message(STATUS "  C++ Standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Sources      : ${MEDIAX_SOURCES}")
message(STATUS "=========================================")
message(STATUS "")
