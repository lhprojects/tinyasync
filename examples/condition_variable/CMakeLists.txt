cmake_minimum_required (VERSION 3.8)

add_executable (condition_variable "condition_variable.cpp")
target_link_libraries(condition_variable PRIVATE Threads::Threads)
