cmake_minimum_required (VERSION 3.8)

add_executable(memory_pool "memory_pool.cpp")

target_link_libraries(memory_pool PRIVATE Threads::Threads)
