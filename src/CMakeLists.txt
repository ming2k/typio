add_library(input_method
    context.c
    key_event.c
    utils.c
)

target_include_directories(input_method
    PUBLIC ${PROJECT_SOURCE_DIR}/include
) 