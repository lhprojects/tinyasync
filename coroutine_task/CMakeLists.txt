cmake_minimum_required (VERSION 3.8)

include_directories(../include)

add_executable(coroutine_task "coroutine_task.cpp" ../include/tinyasync/tinyasync.h)


