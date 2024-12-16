add_executable(simple_input simple_input.c)
add_executable(key_capture key_capture.c)

target_link_libraries(simple_input PRIVATE input_method)
target_link_libraries(key_capture PRIVATE input_method) 