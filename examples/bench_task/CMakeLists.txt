cmake_minimum_required (VERSION 3.8)

add_executable(bench_task "bench_task.cpp")

target_link_libraries(bench_task PRIVATE Threads::Threads)
