cmake_minimum_required (VERSION 3.8)

add_executable(coroutine_task "coroutine_task.cpp")


target_link_libraries(coroutine_task PRIVATE Threads::Threads)
