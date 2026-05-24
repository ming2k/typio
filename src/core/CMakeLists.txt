# ── Rust core crate (typio-core) ────────────────────────────────────────
# The Rust crate provides the entire C ABI. The aggregate `typio-core`
# CMake target wraps the Rust staticlib (whole-archive-linked) and the
# optional built-in basic engine sources.

set(TYPIO_CORE_RUST_DIR    "${CMAKE_CURRENT_SOURCE_DIR}")
set(TYPIO_CORE_TARGET_DIR  "${CMAKE_CURRENT_BINARY_DIR}/cargo")

# Pick cargo profile from CMake build type. Debug-class configs avoid
# --release (no LTO, no opt-level=3) — keeps memory + link time sane while
# iterating. Release builds get the optimized artifact.
set(_cmake_build_type_lower "")
if(CMAKE_BUILD_TYPE)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" _cmake_build_type_lower)
endif()

if(_cmake_build_type_lower STREQUAL "release"
   OR _cmake_build_type_lower STREQUAL "relwithdebinfo"
   OR _cmake_build_type_lower STREQUAL "minsizerel")
    set(_cargo_profile_flag "--release")
    set(_cargo_profile_subdir "release")
else()
    set(_cargo_profile_flag "")
    set(_cargo_profile_subdir "debug")
endif()

set(TYPIO_CORE_RUST_LIB
    "${TYPIO_CORE_TARGET_DIR}/${_cargo_profile_subdir}/libtypio_core.a"
)

# Cross-compilation target (empty for host build)
if(NOT Rust_TARGET)
    set(Rust_TARGET "" CACHE STRING "Rust target triple (leave empty for host)")
endif()
if(Rust_TARGET)
    set(_rust_target_flag "--target=${Rust_TARGET}")
    set(TYPIO_CORE_RUST_LIB
        "${TYPIO_CORE_TARGET_DIR}/${Rust_TARGET}/${_cargo_profile_subdir}/libtypio_core.a"
    )
else()
    set(_rust_target_flag "")
endif()

if(BUILD_BASIC_ENGINE)
    set(_rust_features "--features=build_basic_engine")
else()
    set(_rust_features "")
endif()

file(GLOB_RECURSE TYPIO_CORE_RUST_SOURCES CONFIGURE_DEPENDS
    "${TYPIO_CORE_RUST_DIR}/src/*.rs"
    "${TYPIO_CORE_RUST_DIR}/Cargo.toml"
)

add_custom_command(
    OUTPUT ${TYPIO_CORE_RUST_LIB}
    COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${TYPIO_CORE_TARGET_DIR}
            TYPIO_ENGINE_DIR="${TYPIO_ENGINE_DIR}"
            TYPIO_VERSION="${PROJECT_VERSION}"
            TYPIO_BUILD_SOURCE_LABEL="${TYPIO_BUILD_SOURCE_LABEL}"
        cargo build ${_cargo_profile_flag} ${_rust_target_flag} ${_rust_features}
    DEPENDS ${TYPIO_CORE_RUST_SOURCES}
    WORKING_DIRECTORY ${TYPIO_CORE_RUST_DIR}
    COMMENT "Building typio-core Rust crate (${_cargo_profile_subdir})"
    VERBATIM
)

add_custom_target(typio_core_rust_target DEPENDS ${TYPIO_CORE_RUST_LIB})

add_library(typio_core_rust STATIC IMPORTED GLOBAL)
add_dependencies(typio_core_rust typio_core_rust_target)
set_target_properties(typio_core_rust PROPERTIES
    IMPORTED_LOCATION "${TYPIO_CORE_RUST_LIB}"
)

# CMake requires at least one source file per library target. Generate an
# empty TU so we can build a SHARED library that whole-archive-links the
# Rust staticlib.
set(_typio_core_anchor "${CMAKE_CURRENT_BINARY_DIR}/typio_core_anchor.c")
file(GENERATE OUTPUT ${_typio_core_anchor} CONTENT
    "/* Auto-generated. Forces a C linker invocation for typio-core. */\n"
)

add_library(typio-core
    ${_typio_core_anchor}
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
    -Wl,--whole-archive typio_core_rust -Wl,--no-whole-archive
    pthread
    dl
)

set_target_properties(typio-core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS typio-core
    EXPORT typio-targets
    LIBRARY DESTINATION ${TYPIO_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${TYPIO_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${TYPIO_INSTALL_BINDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/typio/
    DESTINATION ${TYPIO_INSTALL_INCLUDEDIR}/typio
    FILES_MATCHING PATTERN "*.h"
)
