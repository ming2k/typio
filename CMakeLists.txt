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

# Add source directory
add_subdirectory(src)

# Add examples
add_subdirectory(examples)

# Tests (optional)
option(BUILD_TESTS "Build test programs" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif() 