cmake_minimum_required(VERSION 3.20)
project(typio VERSION 3.2.7 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# POSIX features (strdup, clock_gettime, etc.) — centralized here
add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Production-grade compiler warnings
add_compile_options(
    -Wall -Wextra -Wformat=2 -Wconversion -Wsign-conversion
    -Wshadow -Wundef -Wunused
    -Wno-unused-parameter
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(TYPIO_BUILD_SOURCE_LABEL "source")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --always --dirty --tags
        OUTPUT_VARIABLE TYPIO_BUILD_SOURCE_LABEL
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(NOT TYPIO_BUILD_SOURCE_LABEL)
    set(TYPIO_BUILD_SOURCE_LABEL "source")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(BUILD_SHARED_LIBS "Build shared Typio libraries" ON)
option(BUILD_CONTROL_PANEL "Build the GTK4 control panel" OFF)
option(BUILD_TESTS "Build Typio test programs" ON)
option(BUILD_BASIC_ENGINE "Build the built-in basic keyboard engine" ON)
option(BUILD_RIME_ENGINE "Build the Rime engine" OFF)
option(BUILD_MOZC_ENGINE "Build the Mozc Japanese input engine" OFF)
option(ENABLE_WAYLAND "Enable Wayland input-method frontend support" ON)
option(ENABLE_STATUS_BUS "Enable D-Bus runtime status/control interface" ON)
option(ENABLE_SYSTRAY "Enable StatusNotifierItem tray support" OFF)
option(BUILD_WHISPER "Build Whisper voice input support" OFF)
option(BUILD_SHERPA_ONNX "Build sherpa-onnx voice input support" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer (LeakSanitizer included where supported)" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
set(TYPIO_AUTOSTART_DIR "/etc/xdg/autostart" CACHE PATH
    "Directory where the Typio desktop autostart entry is installed")

set(TYPIO_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
set(TYPIO_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
set(TYPIO_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
set(TYPIO_INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}/typio")
set(TYPIO_INSTALL_ENGINEDIR "${CMAKE_INSTALL_LIBDIR}/typio/engines")

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

find_package(PkgConfig REQUIRED)

if(ENABLE_WAYLAND)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
    pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
    include(WaylandProtocol)
    set(HAVE_WAYLAND ON)
endif()

if(ENABLE_STATUS_BUS OR ENABLE_SYSTRAY)
    pkg_check_modules(DBUS REQUIRED dbus-1)
endif()

if(ENABLE_STATUS_BUS)
    set(HAVE_STATUS_BUS ON)
endif()

if(ENABLE_SYSTRAY)
    set(HAVE_SYSTRAY ON)
endif()

if(BUILD_CONTROL_PANEL)
    pkg_check_modules(GTK4 REQUIRED gtk4)
endif()

if(BUILD_RIME_ENGINE)
    pkg_check_modules(RIME REQUIRED rime)
    pkg_get_variable(RIME_PKGDATADIR rime pkgdatadir)
endif()

if(BUILD_MOZC_ENGINE)
    set(HAVE_MOZC ON)
endif()

# Voice input backends — at least one of BUILD_WHISPER / BUILD_SHERPA_ONNX
# is needed for voice input.  PipeWire is required by both.
if(BUILD_WHISPER OR BUILD_SHERPA_ONNX)
    pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
    set(HAVE_VOICE ON)
endif()

if(BUILD_WHISPER)
    # whisper.cpp may be installed under a private prefix (e.g. /usr/lib/whisper.cpp)
    # to avoid file collisions with llama.cpp's bundled ggml.
    set(WHISPER_PREFIX "/usr/lib/whisper.cpp" CACHE PATH
        "Installation prefix of whisper.cpp")
    set(ENV{PKG_CONFIG_PATH}
        "${WHISPER_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

    pkg_check_modules(WHISPER REQUIRED whisper)

    # whisper.pc Libs has wrong link order for static builds and may omit
    # transitive deps (ggml-cpu, OpenMP, C++ runtime).
    # Detect static vs shared and build the correct link list ourselves.
    find_library(WHISPER_LIB whisper HINTS ${WHISPER_LIBRARY_DIRS})
    if(WHISPER_LIB AND WHISPER_LIB MATCHES "\\.a$")
        find_library(GGML_LIB ggml HINTS ${WHISPER_LIBRARY_DIRS})
        find_library(GGML_BASE_LIB ggml-base HINTS ${WHISPER_LIBRARY_DIRS})
        find_library(GGML_CPU_LIB ggml-cpu HINTS ${WHISPER_LIBRARY_DIRS})
        set(WHISPER_LINK_LIBRARIES
            ${WHISPER_LIB}
            ${GGML_LIB} ${GGML_CPU_LIB} ${GGML_BASE_LIB}
            gomp stdc++ m)
    else()
        set(WHISPER_LINK_LIBRARIES ${WHISPER_LIBRARIES})
    endif()

    set(HAVE_WHISPER ON)
endif()

if(BUILD_SHERPA_ONNX)
    set(SHERPA_ONNX_PREFIX "" CACHE PATH
        "Installation prefix of sherpa-onnx (leave empty for system default)")
    if(SHERPA_ONNX_PREFIX)
        set(ENV{PKG_CONFIG_PATH}
            "${SHERPA_ONNX_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        list(APPEND CMAKE_PREFIX_PATH "${SHERPA_ONNX_PREFIX}")
    endif()

    find_library(SHERPA_ONNX_C_LIB sherpa-onnx-c-api
        HINTS "${SHERPA_ONNX_PREFIX}/lib")
    find_path(SHERPA_ONNX_INCLUDE_DIR sherpa-onnx/c-api/c-api.h
        HINTS "${SHERPA_ONNX_PREFIX}/include")

    if(NOT SHERPA_ONNX_C_LIB OR NOT SHERPA_ONNX_INCLUDE_DIR)
        message(FATAL_ERROR "sherpa-onnx not found. Set SHERPA_ONNX_PREFIX or install sherpa-onnx.")
    endif()

    set(SHERPA_ONNX_LINK_LIBRARIES ${SHERPA_ONNX_C_LIB})
    set(SHERPA_ONNX_INCLUDE_DIRS ${SHERPA_ONNX_INCLUDE_DIR})
    set(HAVE_SHERPA_ONNX ON)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/typio_build_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/typio_build_config.h"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/typio.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/typio.pc"
    @ONLY
)

# ── flux (Meson-based, built via ExternalProject) ────────────────────
include(ExternalProject)
find_program(MESON_EXECUTABLE meson REQUIRED)
find_program(NINJA_EXECUTABLE ninja REQUIRED)

# Source resolution order:
#   1. CMake cache variable  -DFLUX_SOURCE_DIR=...
#   2. Environment variable  FLUX_SOURCE_DIR
#   3. Sibling directory     <repo>/../flux
#   4. Git clone (fallback)
set(FLUX_SOURCE_DIR "" CACHE PATH "Path to local flux source checkout (leave empty to clone)")

if(FLUX_SOURCE_DIR)
    set(_flux_src "${FLUX_SOURCE_DIR}")
elseif(DEFINED ENV{FLUX_SOURCE_DIR})
    set(_flux_src "$ENV{FLUX_SOURCE_DIR}")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../flux/meson.build")
    set(_flux_src "${CMAKE_CURRENT_SOURCE_DIR}/../flux")
endif()

set(_flux_build   "${CMAKE_BINARY_DIR}/_flux_build")
set(_flux_prefix  "${CMAKE_BINARY_DIR}/_flux_prefix")
# Static archive — flux is linked into the typio daemon, so the install
# ships no libflux.so and is immune to host-side flux installs at runtime.
set(_flux_lib     "${_flux_prefix}/lib/libflux.a")
set(_flux_inc     "${_flux_prefix}/include")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_flux_buildtype debug)
else()
    set(_flux_buildtype release)
endif()

set(_flux_meson_setup
    ${MESON_EXECUTABLE} setup
        --prefix=${_flux_prefix}
        --libdir=lib
        --buildtype=${_flux_buildtype}
        --default-library=static
        --reconfigure
        -Dcanvas=true
        -Dscene=false
        -Dcompute=false
        -Dexamples=false
        -Dtests=false
        ${_flux_build}
)

if(_flux_src)
    ExternalProject_Add(flux_external
        SOURCE_DIR      "${_flux_src}"
        BINARY_DIR      "${_flux_build}"
        CONFIGURE_COMMAND ${_flux_meson_setup} "${_flux_src}"
        BUILD_COMMAND   ${NINJA_EXECUTABLE} -C "${_flux_build}"
        INSTALL_COMMAND ${NINJA_EXECUTABLE} -C "${_flux_build}" install
        BUILD_BYPRODUCTS "${_flux_lib}"
    )
    message(STATUS "flux: ${_flux_src} (local)")
else()
    set(_flux_clone_src "${CMAKE_BINARY_DIR}/_flux_src")
    ExternalProject_Add(flux_external
        GIT_REPOSITORY  https://github.com/ming2k/flux.git
        GIT_TAG         44de7ec  # v0.0.1
        GIT_SHALLOW     OFF
        SOURCE_DIR      "${_flux_clone_src}"
        BINARY_DIR      "${_flux_build}"
        CONFIGURE_COMMAND ${_flux_meson_setup} "${_flux_clone_src}"
        BUILD_COMMAND   ${NINJA_EXECUTABLE} -C "${_flux_build}"
        INSTALL_COMMAND ${NINJA_EXECUTABLE} -C "${_flux_build}" install
        BUILD_BYPRODUCTS "${_flux_lib}"
    )
    message(STATUS "flux: cloning v0.0.1 from git")
endif()

# Ensure the include directory exists at configure time so that
# target_include_directories doesn't error before the first build.
file(MAKE_DIRECTORY "${_flux_inc}")

# Transitive dependencies of static libflux.a (mirrors what `Requires:` and
# `Libs:` would inject if the library were resolved via pkg-config).
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

add_library(flux::flux STATIC IMPORTED GLOBAL)
add_dependencies(flux::flux flux_external)
set_target_properties(flux::flux PROPERTIES
    IMPORTED_LOCATION             "${_flux_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${_flux_inc}"
    INTERFACE_COMPILE_DEFINITIONS "FLUX_HAVE_CANVAS=1"
    INTERFACE_LINK_LIBRARIES      "Vulkan::Vulkan;Threads::Threads;m"
)

add_subdirectory(src/core)
add_subdirectory(src/engines)
add_subdirectory(src/apps)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/typio.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/typio.toml.example"
    DESTINATION "${TYPIO_INSTALL_DATADIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/hicolor/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor"
)

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/applications/typio.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)

if(BUILD_CONTROL_PANEL)
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/applications/typio-control.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
endif()

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/autostart/typio.desktop"
    DESTINATION "${TYPIO_AUTOSTART_DIR}"
)

install(
    EXPORT typio-targets
    FILE TypioTargets.cmake
    NAMESPACE typio::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/typio"
)
