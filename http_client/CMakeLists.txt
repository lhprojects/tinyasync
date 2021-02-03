cmake_minimum_required (VERSION 3.8)

include_directories(../include)

add_executable(http_client "http_client.cpp" ../include/tinyasync/tinyasync.h)


