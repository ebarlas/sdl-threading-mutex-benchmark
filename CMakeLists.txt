cmake_minimum_required(VERSION 3.19)
project(benchmark)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2)

add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark SDL2::SDL2)