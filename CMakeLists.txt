# Valtrainer - Valorant-calibrated aim trainer (C++17 + raylib + Win32 Raw Input)
#
# Build (from a "Developer PowerShell for VS 2022" or any terminal with CMake):
#   cmake -S . -B build
#   cmake --build build --config Release
# The program ends up in build/Release/Valtrainer.exe

cmake_minimum_required(VERSION 3.25)
project(Valtrainer LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Valtrainer is a Windows application (it uses the Win32 Raw Input API).")
endif()
enable_language(RC)  # for the app icon / version resource

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VALTRAINER_WARNINGS_AS_ERRORS "Fail the build on any compiler warning (used by CI)" OFF)

# Single-config generators (Ninja, MinGW Makefiles) default to Release.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Link the C/C++ runtime statically (MSVC /MT) so Valtrainer.exe runs on any PC
# without installing the Visual C++ Redistributable. The policy default makes
# this apply to raylib too (its CMakeLists predates policy CMP0091).
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# raylib, downloaded automatically the first time you configure.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
# Custom raylib build: identical to the defaults except that raylib's own
# F12 screenshot / Ctrl+F12 GIF capture are off (they saved a stale frame and
# froze the game while writing the PNG). Valtrainer has its own screenshot key.
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_SCREEN_CAPTURE OFF CACHE BOOL "" FORCE)
set(SUPPORT_GIF_RECORDING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM  # treat raylib headers as system headers (no warnings from them)
)
FetchContent_MakeAvailable(raylib)

# raylib is third-party code: silence its own compiler warnings so the build
# output only shows warnings from this project (there should be none).
if(MSVC)
    target_compile_options(raylib PRIVATE /w)
else()
    target_compile_options(raylib PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
add_executable(Valtrainer WIN32
    assets/valtrainer.rc
    src/main.cpp
    src/app.cpp
    src/app.h
    src/app_screens.cpp
    src/audio.cpp
    src/audio.h
    src/camera.cpp
    src/camera.h
    src/config.cpp
    src/config.h
    src/crosshair.cpp
    src/crosshair.h
    src/effects.cpp
    src/effects.h
    src/input.cpp
    src/input.h
    src/modes.cpp
    src/modes.h
    src/vsbot.cpp
    src/movement.cpp
    src/sniper.cpp
    src/modes_extra.cpp
    src/platform.h
    src/platform_win32.cpp
    src/rank.cpp
    src/rank.h
    src/rank_badge.cpp
    src/rank_badge.h
    src/rng.h
    src/selftest.cpp
    src/selftest.h
    src/sens_finder.cpp
    src/sens_finder.h
    src/stats.cpp
    src/stats.h
    src/targets.cpp
    src/targets.h
    src/ui.cpp
    src/ui.h
    src/world.cpp
    src/world.h
)

# Warning flags shared by the app and the self-test tool.
function(valtrainer_setup target)
    target_link_libraries(${target} PRIVATE raylib opengl32 winmm user32 gdi32)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
        if(VALTRAINER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
        if(VALTRAINER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

valtrainer_setup(Valtrainer)
if(MSVC)
    # mainCRTStartup lets a WIN32 (no console) program keep a normal int main().
    target_link_options(Valtrainer PRIVATE /ENTRY:mainCRTStartup)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Valtrainer)
endif()

# ---------------------------------------------------------------------------
# Console tool that runs the math self-test without opening a window
# (used by CI; you can also run build/Release/ValtrainerSelfTest.exe yourself).
add_executable(ValtrainerSelfTest
    src/selftest_main.cpp
    src/selftest.cpp
    src/camera.cpp
    src/crosshair.cpp
    src/rank.cpp
    src/sens_finder.cpp
    src/stats.cpp
)
valtrainer_setup(ValtrainerSelfTest)
