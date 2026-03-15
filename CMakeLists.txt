cmake_minimum_required(VERSION 3.20)
project(typio VERSION 1.4.1 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(BUILD_SHARED_LIBS "Build shared Typio libraries" ON)
option(BUILD_SERVER "Build the Typio Wayland input method daemon" ON)
option(BUILD_CONTROL_PANEL "Build the GTK4 control panel" OFF)
option(BUILD_TESTS "Build Typio test programs" ON)
option(BUILD_BASIC_ENGINE "Build the built-in basic keyboard engine" ON)
option(BUILD_RIME_ENGINE "Build the Rime engine" ON)
option(BUILD_MOZC_ENGINE "Build the Mozc Japanese input engine" OFF)
option(ENABLE_WAYLAND "Enable Wayland input-method frontend support" ON)
option(ENABLE_STATUS_BUS "Enable D-Bus runtime status/control interface" ON)
option(ENABLE_SYSTRAY "Enable StatusNotifierItem tray support" ON)
option(BUILD_WHISPER "Build Whisper voice input support" OFF)
set(TYPIO_AUTOSTART_DIR "/etc/xdg/autostart" CACHE PATH
    "Directory where the Typio desktop autostart entry is installed")

set(TYPIO_INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
set(TYPIO_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
set(TYPIO_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
set(TYPIO_INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}/typio")
set(TYPIO_INSTALL_ENGINEDIR "${CMAKE_INSTALL_LIBDIR}/typio/engines")

find_package(PkgConfig REQUIRED)

if(ENABLE_WAYLAND)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
    pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
    pkg_check_modules(CAIRO_PANGO REQUIRED cairo pangocairo)
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

if(BUILD_WHISPER)
    # whisper.cpp may be installed under a private prefix (e.g. /usr/lib/whisper.cpp)
    # to avoid file collisions with llama.cpp's bundled ggml.
    set(WHISPER_PREFIX "/usr/lib/whisper.cpp" CACHE PATH
        "Installation prefix of whisper.cpp")
    set(ENV{PKG_CONFIG_PATH}
        "${WHISPER_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

    pkg_check_modules(WHISPER REQUIRED whisper)
    pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

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

add_subdirectory(src/lib)
add_subdirectory(src/engines)

if(BUILD_SERVER)
    add_subdirectory(src/server)
endif()

if(BUILD_CONTROL_PANEL)
    add_subdirectory(src/control)
endif()

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

if(BUILD_CONTROL_PANEL)
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/applications/typio-control.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
endif()

if(BUILD_SERVER)
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/autostart/typio.desktop"
        DESTINATION "${TYPIO_AUTOSTART_DIR}"
    )
endif()

install(
    EXPORT typio-targets
    FILE TypioTargets.cmake
    NAMESPACE typio::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/typio"
)
