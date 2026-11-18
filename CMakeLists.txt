cmake_minimum_required(VERSION 3.31)
project(felis_notebook VERSION 0.6.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL
    "Scan C++ sources for module dependencies")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(FELIS_BUILD_NET ON CACHE BOOL "" FORCE)
set(FELIS_BUILD_SQLITE3 ON CACHE BOOL "" FORCE)
set(FELIS_BUILD_IMAGE OFF CACHE BOOL "" FORCE)
set(FELIS_BUILD_AUDIO OFF CACHE BOOL "" FORCE)


# ========== ---------- ---------- ---------- ==========
#                   PLATFORM VARIABLES
# ========== ---------- ---------- ---------- ==========


# Platform string
if(WIN32)
    set(PLATFORM "windows")
elseif(APPLE)
    set(PLATFORM "macos")
elseif(UNIX)
    set(PLATFORM "linux")
else()
    set(PLATFORM "unknown")
endif()


# Architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(ARCH "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    set(ARCH "arm64")
else()
    set(ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Qt 6.8 requires Windows 10, so declare that as the minimum target for every
# translation unit. This also stops Boost.Asio from assuming Windows 7, and
# NOMINMAX/WIN32_LEAN_AND_MEAN keep the Windows SDK from defining `min`, `max`
# and COM's `interface` macros, which otherwise collide with ordinary C++
# identifiers.
if(WIN32)
    add_compile_definitions(
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
endif()

# Boost.Beast/Asio translation units are large enough to exceed MSVC's default
# object-file section limit.
if(MSVC)
    add_compile_options(/bigobj)
endif()

# Debug-only diagnostics (request-body dumps and the like). The configure presets
# set the cache variable, so `linux-x64-debug` turns it on and a plain build stays
# quiet. The macro is named `LOCAL_NOTEBOOK_DEBUG` rather than `_DEBUG` on
# purpose: MSVC already defines `_DEBUG` for Debug configurations and uses it to
# select the debug CRT, so claiming that name would conflate our switch with the
# toolchain's.
set(LOCAL_NOTEBOOK_DEBUG OFF CACHE BOOL
    "Compile debug-only diagnostics into the binaries")
if(LOCAL_NOTEBOOK_DEBUG)
    add_compile_definitions(
        LOCAL_NOTEBOOK_DEBUG=true
        # felis guards its own diagnostics with `if constexpr (FELIS_DEBUG)` and
        # defaults the macro to `false` unless something defines it first, so a
        # command-line definition takes precedence. It is consumed by `if
        # constexpr`, not `#if`, hence `true` rather than `1`.
        # FELIS_DEBUG=true
    )
endif()


# ========== ---------- ---------- ---------- ==========
#                    SUB  DIRECTORIES
# ========== ---------- ---------- ---------- ==========

add_subdirectory(external/felis)
add_subdirectory(bell)
add_subdirectory(ui)
