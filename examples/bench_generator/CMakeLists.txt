cmake_minimum_required (VERSION 3.8)

add_executable(bench_generator "bench_generator.cpp")

target_link_libraries(bench_generator PRIVATE Threads::Threads)
