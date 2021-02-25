cmake_minimum_required (VERSION 3.8)

add_executable(http_helloworld_server "http_helloworld_server.cpp")


target_link_libraries(http_helloworld_server PRIVATE Threads::Threads)
