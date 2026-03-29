add_executable(typio-client main.c)

target_include_directories(typio-client PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lib
    ${DBUS_INCLUDE_DIRS}
)

target_link_libraries(typio-client PRIVATE ${DBUS_LIBRARIES})

set_target_properties(typio-client PROPERTIES OUTPUT_NAME typio-client)

install(TARGETS typio-client RUNTIME DESTINATION "${TYPIO_INSTALL_BINDIR}")
