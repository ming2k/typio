# WaylandProtocol.cmake
# CMake functions for generating Wayland protocol code using wayland-scanner

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# Generate client-side protocol code from XML
#
# Usage:
#   wayland_generate_protocol(
#     PROTOCOL_XML <path-to-protocol.xml>
#     GENERATED_SOURCES <variable-for-generated-sources>
#     [CLIENT_HEADER <variable-for-client-header>]
#   )
#
# This function generates:
#   - Client header: ${name}-client-protocol.h
#   - Client code: ${name}-protocol.c
#
function(wayland_generate_protocol)
    cmake_parse_arguments(PARSE_ARGV 0 WGP
        ""
        "PROTOCOL_XML;GENERATED_SOURCES;CLIENT_HEADER"
        ""
    )

    if(NOT WGP_PROTOCOL_XML)
        message(FATAL_ERROR "wayland_generate_protocol: PROTOCOL_XML is required")
    endif()
    if(NOT WGP_GENERATED_SOURCES)
        message(FATAL_ERROR "wayland_generate_protocol: GENERATED_SOURCES is required")
    endif()

    get_filename_component(protocol_name ${WGP_PROTOCOL_XML} NAME_WE)
    set(output_dir ${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols)
    file(MAKE_DIRECTORY ${output_dir})

    set(client_header ${output_dir}/${protocol_name}-client-protocol.h)
    set(client_code ${output_dir}/${protocol_name}-protocol.c)

    # Generate client header
    add_custom_command(
        OUTPUT ${client_header}
        COMMAND ${WAYLAND_SCANNER} client-header ${WGP_PROTOCOL_XML} ${client_header}
        DEPENDS ${WGP_PROTOCOL_XML}
        COMMENT "Generating Wayland client header: ${protocol_name}"
        VERBATIM
    )

    # Generate client code
    add_custom_command(
        OUTPUT ${client_code}
        COMMAND ${WAYLAND_SCANNER} private-code ${WGP_PROTOCOL_XML} ${client_code}
        DEPENDS ${WGP_PROTOCOL_XML}
        COMMENT "Generating Wayland client code: ${protocol_name}"
        VERBATIM
    )

    set(${WGP_GENERATED_SOURCES} ${client_header} ${client_code} PARENT_SCOPE)

    if(WGP_CLIENT_HEADER)
        set(${WGP_CLIENT_HEADER} ${client_header} PARENT_SCOPE)
    endif()
endfunction()
