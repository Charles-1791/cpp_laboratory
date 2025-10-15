cmake_minimum_required(VERSION 3.10)
project(test_project)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(test_exec test.cpp)


find_package(Threads REQUIRED)
target_link_libraries(test_exec Threads::Threads)
