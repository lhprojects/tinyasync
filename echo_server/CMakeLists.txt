cmake_minimum_required (VERSION 3.8)

include_directories(../include)


add_executable(echo_server "echo_server.cpp"  ../include/tinyasync/tinyasync.h)


