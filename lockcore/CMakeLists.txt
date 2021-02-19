cmake_minimum_required (VERSION 3.8)

add_executable (lockcore "lockcore.cpp")
target_link_libraries(lockcore PRIVATE Threads::Threads)
