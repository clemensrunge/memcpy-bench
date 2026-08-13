cmake_minimum_required(VERSION 3.28)
project(memcpy-bench C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(memcpy-bench main.c Benchmark.c MemoryInfo.c CpuIdFunctions.cpp)

if (UNIX)
    target_link_libraries(memcpy-bench PRIVATE m)
endif ()
