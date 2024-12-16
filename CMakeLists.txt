cmake_minimum_required(VERSION 3.10)
project(typio VERSION 0.1.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Main library
add_library(input_method
    src/context.c
    src/key_event.c
    src/utils.c
)

target_include_directories(input_method
    PUBLIC include
)

# Example program
add_executable(simple_input examples/simple_input.c)
add_executable(key_capture examples/key_capture.c)
target_link_libraries(simple_input PRIVATE input_method)
target_link_libraries(key_capture PRIVATE input_method)

# Tests (optional)
option(BUILD_TESTS "Build test programs" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_input_method tests/test_input_method.c)
    add_executable(manual_test tests/manual_test.c)
    target_link_libraries(test_input_method PRIVATE input_method)
    target_link_libraries(manual_test PRIVATE input_method)
    add_test(NAME test_input_method COMMAND test_input_method)
endif() 