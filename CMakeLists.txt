cmake_minimum_required(VERSION 3.25)
project(dogdouclix VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Global compiler configuration for MSVC on Windows
if(MSVC)
    add_compile_options(
        /utf-8
        /W4
        /permissive-
        /Zc:preprocessor
        /EHsc
    )
    add_compile_definitions(
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Enable testing
enable_testing()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Subdirectories
add_subdirectory(src/lib)
add_subdirectory(src/cli)
add_subdirectory(tests)