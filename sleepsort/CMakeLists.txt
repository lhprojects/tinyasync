cmake_minimum_required (VERSION 3.8)

include_directories(../include)
add_executable (sleepsort "sleepsort.cpp"  ../include/tinyasync/tinyasync.h)

