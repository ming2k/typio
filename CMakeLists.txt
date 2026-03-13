cmake_minimum_required(VERSION 3.16)
project(typio VERSION 1.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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
option(BUILD_TESTS "Build Typio test programs" ON)
option(BUILD_BASIC_ENGINE "Build the built-in basic keyboard engine" ON)
option(BUILD_RIME_ENGINE "Build the Rime engine" ON)
option(ENABLE_WAYLAND "Enable Wayland input-method frontend support" ON)
option(ENABLE_SYSTRAY "Enable StatusNotifierItem tray support" ON)

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

if(ENABLE_SYSTRAY)
    pkg_check_modules(DBUS REQUIRED dbus-1)
    set(HAVE_SYSTRAY ON)
endif()

if(BUILD_RIME_ENGINE)
    pkg_check_modules(RIME REQUIRED rime)
    pkg_get_variable(RIME_PKGDATADIR rime pkgdatadir)
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

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/typio.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/typio.conf.example"
    DESTINATION "${TYPIO_INSTALL_DATADIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/hicolor/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/engines/"
    DESTINATION "${TYPIO_INSTALL_DATADIR}/engines"
    FILES_MATCHING PATTERN "*.example"
)

install(
    EXPORT typio-targets
    FILE TypioTargets.cmake
    NAMESPACE typio::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/typio"
)
