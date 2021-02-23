cmake_minimum_required (VERSION 3.8)

project ("tinasync")

find_package(Threads REQUIRED)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/std:c++latest)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-fcoroutines -std=c++2a -Wfatal-errors)
else()
    add_compile_options(-fcoroutines-ts -std=c++2a -Wfatal-errors -stdlib=libc++)
endif()

include_directories(../include)
set(HEADER_FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tinyasync/basics.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tinyasync/task.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tinyasync/io_context.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tinyasync/awaiters.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tinyasync/mutex.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/tinyasync/tinyasync.h
)


add_subdirectory("bench_task")
add_subdirectory("chatroom_server")
add_subdirectory("condition_variable")
add_subdirectory("coroutine_task")
add_subdirectory("echo_server")
add_subdirectory("http_client")
add_subdirectory("http_helloworld_server")
add_subdirectory("lockcore")
add_subdirectory("mutex")
add_subdirectory("pingpong")
add_subdirectory("sleepsort")
add_subdirectory("wait")

