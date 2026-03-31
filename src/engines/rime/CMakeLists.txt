add_library(typio-engine-rime MODULE
    path_expand.c
    rime_engine.c
)

target_include_directories(typio-engine-rime PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${RIME_INCLUDE_DIRS}
)

target_link_libraries(typio-engine-rime PRIVATE
    typio-core
    ${RIME_LIBRARIES}
)

target_compile_options(typio-engine-rime PRIVATE ${RIME_CFLAGS_OTHER})

if(RIME_PKGDATADIR)
    target_compile_definitions(typio-engine-rime PRIVATE
        TYPIO_RIME_SHARED_DATA_DIR="${RIME_PKGDATADIR}"
    )
endif()

set_target_properties(typio-engine-rime PROPERTIES
    PREFIX ""
    OUTPUT_NAME "rime"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/engines"
    BUILD_RPATH "${CMAKE_BINARY_DIR}/src/core"
    INSTALL_RPATH "\$ORIGIN/../.."
)

install(TARGETS typio-engine-rime
    LIBRARY DESTINATION "${TYPIO_INSTALL_ENGINEDIR}"
)
