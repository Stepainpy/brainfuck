cmake_minimum_required(VERSION 3.27)
project(brainfuck VERSION 1.0 LANGUAGES C
    DESCRIPTION "Brainfuck virtual machine with optimizations"
)

set(CMAKE_COLOR_MAKEFILE OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra -pedantic")

include_directories(inc)

add_library(brainfuck STATIC
    src/bfcompile.c
    src/bfexecute.c
    src/bfdebug.c
    src/bfother.c
)

add_executable(bf bf.c)
target_link_libraries(bf PRIVATE brainfuck)