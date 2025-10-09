cmake_minimum_required(VERSION 3.15)
project(bloomc)

# Set C++17 as the minimum standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(bloomc
    src/allocation.cpp
    src/parsing.cpp
    src/print.cpp
    src/string.cpp
    src/tokenization.cpp
    src/main.cpp
)

target_include_directories(bloomc
    PRIVATE
        include
)