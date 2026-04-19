cmake_minimum_required(VERSION 3.27)
project(dsortcpp)

set(CMAKE_CXX_STANDARD 17)

add_executable(dsortcpp
        dialsort_benchmark_v2.cpp)

add_executable(bench_counting
        bench_vs_dialsort_vs_counting_bench.cpp
        Dialsortvsskasort.cpp
        Dialsortvsips4oparallel.cpp)

add_executable(bench_ips4o
        DialsortvsIps4o.cpp)

target_include_directories(bench_ips4o PRIVATE
        ${CMAKE_SOURCE_DIR}/ips4o/include)

target_compile_definitions(bench_ips4o PRIVATE
        SKIP_IPS4O)