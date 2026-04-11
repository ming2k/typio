# CMake configuration to link Skia (built externally via GN)

set(SKIA_DIR "${CMAKE_SOURCE_DIR}/external/skia")
set(SKIA_OUT_DIR "${SKIA_DIR}/out/Release")

# Include paths for Skia headers
target_include_directories(typio-daemon PRIVATE 
    ${SKIA_DIR}
    ${SKIA_DIR}/include
    ${SKIA_DIR}/include/core
    ${SKIA_DIR}/include/gpu
    ${SKIA_DIR}/include/paragraph
)

# Link against the static Skia library
target_link_libraries(typio-daemon PRIVATE
    ${SKIA_OUT_DIR}/libskia.a
    # Link required system libraries for Skia
    dl
    pthread
    fontconfig
    freetype
)

# Ensure C++17 or higher for Skia
target_compile_features(typio-daemon PRIVATE cxx_std_17)
