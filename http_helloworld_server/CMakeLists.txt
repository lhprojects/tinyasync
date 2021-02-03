cmake_minimum_required (VERSION 3.8)

include_directories(../include)

add_executable(http_helloworld_server "http_helloworld_server.cpp" ../include/tinyasync/tinyasync.h)


