enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)

# Generate C++ sources from the minimal Mozc proto subset
protobuf_generate_cpp(MOZC_PROTO_SRCS MOZC_PROTO_HDRS
    proto/mozc_commands.proto
)

add_library(typio-engine-mozc MODULE
    mozc_engine.cc
    ${MOZC_PROTO_SRCS}
    ${MOZC_PROTO_HDRS}
)

target_include_directories(typio-engine-mozc PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_CURRENT_BINARY_DIR}   # generated proto headers
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(typio-engine-mozc PRIVATE
    typio-core
    ${Protobuf_LIBRARIES}
)

set_target_properties(typio-engine-mozc PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mozc"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engines"
    BUILD_RPATH "${CMAKE_BINARY_DIR}/src/core"
    INSTALL_RPATH "\$ORIGIN/../.."
)

install(TARGETS typio-engine-mozc
    LIBRARY DESTINATION "${TYPIO_INSTALL_ENGINEDIR}"
)
