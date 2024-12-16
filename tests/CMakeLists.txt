add_executable(test_input_method test_input_method.c)
add_executable(manual_test manual_test.c)

target_link_libraries(test_input_method PRIVATE input_method)
target_link_libraries(manual_test PRIVATE input_method)

add_test(NAME test_input_method COMMAND test_input_method)
add_test(NAME manual_test COMMAND manual_test) 