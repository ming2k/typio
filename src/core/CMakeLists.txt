# Core library
add_library(typio-core
    runtime/build_info.c
    runtime/instance.c
    runtime/engine.c
    runtime/engine_label.c
    runtime/engine_manager.c
    runtime/input_context.c
    runtime/event.c
    runtime/config.c
    runtime/config_schema.c
    runtime/rime_schema_list.c
    utils/arena.c
    utils/list.c
    utils/string.c
    utils/log.c
    $<$<BOOL:${BUILD_BASIC_ENGINE}>:${CMAKE_SOURCE_DIR}/src/engines/basic/basic.c>
)

target_include_directories(typio-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(typio-core PRIVATE ${CMAKE_DL_LIBS})

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
