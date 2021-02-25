cmake_minimum_required (VERSION 3.8)

add_executable (sleepsort "sleepsort.cpp")
target_link_libraries(sleepsort PRIVATE Threads::Threads)
