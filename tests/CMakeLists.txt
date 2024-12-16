enable_testing()

add_executable(test_input_method test_input_method.c)
target_link_libraries(test_input_method PRIVATE input_method)

add_test(NAME test_input_method COMMAND test_input_method) 