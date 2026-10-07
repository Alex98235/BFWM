cmake_minimum_required(VERSION 3.15)
project(BFWM CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C standard (for external C libraries that don't set it themselves)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)


# Opt-in ETW profiling build: emit CodeView debug info and a PDB (via lld) so
# Windows Performance Analyzer can sample BFWM. OFF by default — the normal
# GNU ld build is byte-for-byte unchanged when this option is disabled.
option(BFWM_PROFILE_BUILD "Build with CodeView debug info and PDB for ETW profiling" OFF)
if(BFWM_PROFILE_BUILD)
    add_compile_options(-g -gcodeview)
    add_link_options(-fuse-ld=lld)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-g -Wall -Wpedantic -Wextra)
# WINVER/_WIN32_WINNT 0x0A00 gates the Per-Monitor V2 DPI APIs
# (SetProcessDpiAwarenessContext, GetDpiForWindow, GetDpiForMonitor).
add_compile_definitions(UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)

# Build Lua from vendored source (no system dependency)
add_subdirectory(external)

# Print status messages
message(STATUS "Source directory: ${CMAKE_CURRENT_SOURCE_DIR}")

# ============================================================================
# Library sources — explicitly listed, grouped by module
# ============================================================================

# Core
set(CORE_SOURCES
    src/core/action_dispatcher.cpp
    src/core/bfwm_context.cpp
    src/core/bfwm_def.h
    src/core/portable.cpp
)

# Memory
set(MEMORY_SOURCES
    src/memory/safe.cpp
)

# Math
set(MATH_SOURCES
    src/math/rect.cpp
)

# Transaction (batched window operations)
set(TRANSACTION_SOURCES
    src/transaction/transaction.cpp
    src/transaction/spsc_queue.cpp
    src/transaction/operation.h
)

# Config
set(CONFIG_SOURCES
    src/config/defaults.cpp
    src/config/lua/parser.cpp
    src/config/lua/builder.cpp
    src/config/config_paths.cpp
    src/config/keybinds.cpp
    src/config/action.cpp
)

# Input pipeline
set(INPUT_SOURCES
    src/input/keystroke.cpp
    src/input/hooks.cpp
    src/input/queue.cpp
    src/input/actions/handlers.cpp
)

# Logging
set(LOGGING_SOURCES
    src/logging/logger.cpp
)

# Monitor
set(MONITOR_SOURCES
    src/monitor/monitor.cpp
)

# DPI (per-monitor DPI truth, scaling math, process awareness)
set(DPI_SOURCES
    src/dpi/dpi.cpp
)

# Bar
set(BAR_SOURCES
    src/bar/bar.cpp
    src/bar/renderer/clay_gdi.cpp
)

# System
set(SYSTEM_SOURCES
    src/system/system_status.cpp
)

# Window
set(WINDOW_SOURCES
    src/window/window.cpp
    src/window/focus.cpp
    src/window/cleanup.cpp
    src/window/cloaking.cpp
    src/window/filter.cpp
    src/window/events/events.cpp
    src/window/fullscreen/detect.cpp
    src/window/rules/rules.cpp
)

# Window border service (pure queue-routing layer) + overlay render module
set(BORDER_SOURCES
    src/window/border/border_manager.cpp
    src/window/border/overlay.cpp
)

# WM
set(WM_SOURCES
    src/wm/wm_worker.cpp
)

# Debug / crash dump
set(DEBUG_SOURCES
    src/debug/crash_dump.cpp
)

# Notification snackbar
set(NOTIFICATION_SOURCES
    src/notification/snackbar.cpp
)

# Win32 utilities
set(WIN_SOURCES
    src/win/win_utils.cpp
    src/win/win_error.cpp
)

# System tray icon (hidden window + Shell_NotifyIcon)
set(TRAY_SOURCES
    src/tray/tray.cpp
)

# Workspace
set(WORKSPACE_SOURCES
    src/workspace/workspace.cpp
    src/workspace/placement.cpp
    src/workspace/layouts/dwindle/dwindle.cpp
    src/workspace/layouts/dwindle/dwindle_tree.cpp
    src/workspace/layouts/dwindle/dwindle_vtable.cpp
    src/workspace/layouts/monocle/monocle.cpp
    src/workspace/layouts/monocle/monocle_vtable.cpp
    src/workspace/layouts/master/master.cpp
    src/workspace/layouts/master/master_vtable.cpp
)

# Combine all library sources (everything except main.cpp)
set(LIB_SOURCES
    ${CORE_SOURCES}
    ${MEMORY_SOURCES}
    ${MATH_SOURCES}
    ${TRANSACTION_SOURCES}
    ${CONFIG_SOURCES}
    ${INPUT_SOURCES}
    ${LOGGING_SOURCES}
    ${DEBUG_SOURCES}
    ${MONITOR_SOURCES}
    ${DPI_SOURCES}
    ${BAR_SOURCES}
    ${SYSTEM_SOURCES}
    ${WINDOW_SOURCES}
    ${BORDER_SOURCES}
    ${WM_SOURCES}
    ${WORKSPACE_SOURCES}
    ${NOTIFICATION_SOURCES}
    ${WIN_SOURCES}
    ${TRAY_SOURCES}
)

message(STATUS "Library sources: ${LIB_SOURCES}")

# Tests
enable_testing()
add_subdirectory(test)

# ============================================================================
# Targets
# ============================================================================

# Main executable (dpi.rc embeds the PerMonitorV2 manifest via windres)
add_executable(BFWM WIN32 src/main.cpp src/dpi/dpi.rc ${LIB_SOURCES})

# Icon resource: embedded only when icons/BFWM.ico exists. Generate it with
# scripts/build_icon.ps1 (assembles the PNG sources in icons/ into one .ico).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icons/BFWM.ico")
    target_sources(BFWM PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    # Rebuild the resource object when the .ico changes (windres reads it via app.rc)
    set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/app.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/icons/BFWM.ico")
    message(STATUS "Embedding icon resource: icons/BFWM.ico")
else()
    message(STATUS "icons/BFWM.ico not found - run scripts/build_icon.ps1 to generate it (icon resource skipped)")
endif()

# Version: single source of truth is the VERSION file at the repo root. The
# leading numeric triple must stay parseable even for pre-release suffixes
# (e.g. "0.0.1-pre-release").
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BFWM_VERSION_RAW)
string(STRIP "${BFWM_VERSION_RAW}" BFWM_VERSION)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" BFWM_VERSION_MATCH "${BFWM_VERSION}")
set(BFWM_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(BFWM_VERSION_MINOR "${CMAKE_MATCH_2}")
set(BFWM_VERSION_PATCH "${CMAKE_MATCH_3}")
configure_file(src/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
# Re-run configure when VERSION changes so the version resource stays in sync.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(BFWM PRIVATE
        -Wctor-dtor-privacy -Wnon-virtual-dtor
    )
endif()

target_link_libraries(BFWM PRIVATE dwmapi gdiplus lua tiny-regex-c clay ole32 oleaut32 psapi wlanapi shcore)
message(STATUS "Created executable: BFWM")
message(STATUS "Created executable: BFWM-debug (debug/console)")
add_custom_target(run-debug
    COMMAND $<TARGET_FILE:BFWM-debug>
    DEPENDS BFWM-debug
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/debug
    COMMENT "Running BFWM (debug build)"
    USES_TERMINAL
)

# Each executable needs a distinct PDB name, so the --pdb flag is scoped to the
# BFWM target only (the target that matters for profiling).
if(BFWM_PROFILE_BUILD)
    target_link_options(BFWM PRIVATE "-Wl,--pdb=BFWM.pdb")
endif()

# ============================================================================
# Options
# ============================================================================

option(BFWM_DEBUG_CRASHDUMP "Enable crash dump VEH handler" OFF)
if(BFWM_DEBUG_CRASHDUMP)
    target_compile_definitions(BFWM PRIVATE DEBUG)
endif()

# ============================================================================
# Custom targets
# ============================================================================

add_custom_target(run
    COMMAND $<TARGET_FILE:BFWM>
    DEPENDS BFWM
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running BFWM"
    USES_TERMINAL
)

# Debug build: a separate console-subsystem executable compiled with -DDEBUG
# (log level Debug) and its own output directory, so it never overwrites the
# production (GUI) build. See run-debug target below.
add_executable(BFWM-debug src/main.cpp src/dpi/dpi.rc ${LIB_SOURCES})
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icons/BFWM.ico")
    target_sources(BFWM-debug PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/app.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/icons/BFWM.ico")
endif()
set_target_properties(BFWM-debug PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/debug)
target_compile_definitions(BFWM-debug PRIVATE DEBUG)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(BFWM-debug PRIVATE
        -Wctor-dtor-privacy -Wnon-virtual-dtor
    )
endif()
target_link_libraries(BFWM-debug PRIVATE dwmapi gdiplus lua tiny-regex-c clay ole32 oleaut32 psapi wlanapi shcore)
if(BFWM_PROFILE_BUILD)
    target_link_options(BFWM-debug PRIVATE "-Wl,--pdb=BFWM-debug.pdb")
endif()

# Copy compile_commands.json to source root so clangd and clang-tidy
# can find it regardless of which build directory is active. They search
# upward from each source file and look for this file at the project root.
# This runs on every build, keeping the root copy in sync.
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
    COMMENT "Copying compile_commands.json to project root for clangd/clang-tidy"
)

message(STATUS "Executables: ${CMAKE_BINARY_DIR}/BFWM (prod/GUI) and ${CMAKE_BINARY_DIR}/debug/BFWM-debug (debug/console)")
