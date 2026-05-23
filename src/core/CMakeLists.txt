# ── Rust core (typio-core-rs) ───────────────────────────────────────────
set(TYPIO_CORE_RS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../core-rs")
set(TYPIO_CORE_RS_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(TYPIO_CORE_RS_LIB "${TYPIO_CORE_RS_TARGET_DIR}/release/libtypio_core.a")

# Determine Rust target triple if cross-compiling
if(NOT Rust_TARGET)
    set(Rust_TARGET "" CACHE STRING "Rust target triple (leave empty for host)")
endif()
if(Rust_TARGET)
    set(_rust_target_flag "--target=${Rust_TARGET}")
else()
    set(_rust_target_flag "")
endif()

add_custom_command(
    OUTPUT ${TYPIO_CORE_RS_LIB}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${TYPIO_CORE_RS_TARGET_DIR}
        cargo build --release ${_rust_target_flag}
    WORKING_DIRECTORY ${TYPIO_CORE_RS_DIR}
    COMMENT "Building typio-core Rust crate"
    VERBATIM
)

add_custom_target(typio_core_rs_target DEPENDS ${TYPIO_CORE_RS_LIB})

add_library(typio_core_rs STATIC IMPORTED GLOBAL)
add_dependencies(typio_core_rs typio_core_rs_target)
set_target_properties(typio_core_rs PROPERTIES
    IMPORTED_LOCATION "${TYPIO_CORE_RS_LIB}"
)

# Core library
add_library(typio-core
    runtime/build_info.c
    runtime/instance.c
    runtime/engine.c
    runtime/engine_label.c
    runtime/engine_manager.c
    runtime/input_context.c
    runtime/event.c
    utils/arena.c
    utils/list.c
    utils/string.c
    utils/log.c
    utils/result.c
    $<$<BOOL:${BUILD_BASIC_ENGINE}>:${CMAKE_SOURCE_DIR}/src/engines/basic/basic.c>
    $<$<BOOL:${BUILD_BASIC_ENGINE}>:${CMAKE_SOURCE_DIR}/src/engines/basic/compose.c>
)

target_include_directories(typio-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(typio-core PRIVATE
    ${CMAKE_DL_LIBS}
    -Wl,--whole-archive typio_core_rs -Wl,--no-whole-archive
    pthread
    dl
)

set_target_properties(typio-core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Install
install(TARGETS typio-core
    EXPORT typio-targets
    LIBRARY DESTINATION ${TYPIO_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${TYPIO_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${TYPIO_INSTALL_BINDIR}
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/typio/
    DESTINATION ${TYPIO_INSTALL_INCLUDEDIR}/typio
    FILES_MATCHING PATTERN "*.h"
)
