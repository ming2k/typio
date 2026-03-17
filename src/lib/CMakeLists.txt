# Core library
add_library(typio-core
    core/instance.c
    core/engine.c
    core/engine_label.c
    core/engine_manager.c
    core/input_context.c
    core/event.c
    core/config.c
    core/config_schema.c
    core/rime_schema_list.c
    utils/list.c
    utils/string.c
    utils/log.c
    $<$<BOOL:${BUILD_BASIC_ENGINE}>:${CMAKE_SOURCE_DIR}/src/engines/basic/basic.c>
)

target_include_directories(typio-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
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
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/typio/
    DESTINATION ${TYPIO_INSTALL_INCLUDEDIR}/typio
    FILES_MATCHING PATTERN "*.h"
)
