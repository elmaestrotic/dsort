cmake_minimum_required(VERSION 3.27)
project(dsortcpp)

set(CMAKE_CXX_STANDARD 17)

add_executable(dsortcpp
        dialsort_benchmark_v2.cpp)

add_executable(bench_vs
        "bench_vs dialsort_vs_counting_bench.cpp")
