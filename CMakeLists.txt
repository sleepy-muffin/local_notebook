cmake_minimum_required(VERSION 3.31)
project(local_notebook LANGUAGES CXX)

enable_testing()

# No translation unit uses C++20 modules. CMake otherwise scans every source
# with clang-scan-deps whenever the compiler supports it (clang does), and that
# tool is packaged separately as clang-tools rather than with the compiler, so
# builds with only clang installed fail. It also adds a scan step per file for
# no benefit. Override the cache entry to enable scanning.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL
    "Scan C++ sources for module dependencies")

# One language standard and one compile-command database for every submodule.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Only the standard-library core, the Boost.JSON helpers and the SQLite wrapper
# are used. Turning the image and audio modules off also stops CMake from
# probing for OpenGL, JPEG, PNG and the audio codecs. The options are forced so
# a cache left over from an earlier configure cannot resurrect those probes.
set(SHARK_BUILD_IMAGE OFF CACHE BOOL "Build shark's image module" FORCE)
set(SHARK_BUILD_AUDIO OFF CACHE BOOL "Build shark's audio module" FORCE)

# ========== ---------- ---------- ---------- ==========
#                     SUBMODULE HELPER
# ========== ---------- ---------- ---------- ==========


# Each component lives in its own subdirectory and contributes exactly one
# library, exposed under the `local_notebook::<name>` namespace. Consumers link
# `local_notebook::core`, `local_notebook::ui`, `local_notebook::render`, ...
# instead of hand-assembling target names out of `-core`/`-ui` suffixes, so the
# submodules stay independent and can be reordered or reused freely.
#
#   local_notebook_add_module(<name> STATIC
#       SOURCES          <files...>
#       [PUBLIC_INCLUDES  <dirs...>]
#       [PRIVATE_INCLUDES <dirs...>]
#       [PUBLIC           <targets...>]
#       [PRIVATE          <targets...>])
function(local_notebook_add_module name type)
    cmake_parse_arguments(ARG ""
        ""
        "SOURCES;PUBLIC_INCLUDES;PRIVATE_INCLUDES;PUBLIC;PRIVATE"
        ${ARGN})

    set(target "local_notebook_${name}")

    add_library(${target} ${type} ${ARG_SOURCES})
    add_library(local_notebook::${name} ALIAS ${target})

    if(ARG_PUBLIC_INCLUDES)
        target_include_directories(${target} PUBLIC ${ARG_PUBLIC_INCLUDES})
    endif()
    if(ARG_PRIVATE_INCLUDES)
        target_include_directories(${target} PRIVATE ${ARG_PRIVATE_INCLUDES})
    endif()

    if(ARG_PUBLIC)
        target_link_libraries(${target} PUBLIC ${ARG_PUBLIC})
    endif()
    if(ARG_PRIVATE)
        target_link_libraries(${target} PRIVATE ${ARG_PRIVATE})
    endif()

    if(type STREQUAL "STATIC")
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME "local_notebook-${name}-${PLATFORM}-${ARCH}")
    endif()
endfunction()

add_subdirectory(external/shark)
add_subdirectory(core)
add_subdirectory(ui)
