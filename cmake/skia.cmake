# CMake configuration to link Skia (built externally via GN)

set(SKIA_DIR "${CMAKE_SOURCE_DIR}/external/skia")
set(SKIA_OUT_DIR "${SKIA_DIR}/out/Release")

# Include paths for Skia headers — SYSTEM suppresses warnings from Skia's own headers
target_include_directories(typio SYSTEM PRIVATE
    ${SKIA_DIR}
    ${SKIA_DIR}/include
    ${SKIA_DIR}/include/core
    ${SKIA_DIR}/include/gpu
    ${SKIA_DIR}/modules/skparagraph/include
    ${SKIA_DIR}/modules/skunicode/include
)

# Link against Skia static libraries
target_link_libraries(typio PRIVATE
    ${SKIA_OUT_DIR}/libskparagraph.a
    ${SKIA_OUT_DIR}/libskshaper.a
    ${SKIA_OUT_DIR}/libskunicode_icu.a
    ${SKIA_OUT_DIR}/libskunicode_core.a
    ${SKIA_OUT_DIR}/libskia.a
    # Required system libraries
    dl
    pthread
    fontconfig
    freetype
    harfbuzz
    icuuc
)

# Ensure C++17 or higher for Skia
set_target_properties(typio PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
