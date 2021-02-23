cmake_minimum_required (VERSION 3.8)

add_executable (wait "wait.cpp")
target_link_libraries(wait PRIVATE Threads::Threads)
