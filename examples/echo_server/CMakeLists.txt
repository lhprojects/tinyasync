﻿cmake_minimum_required (VERSION 3.8)

add_executable(echo_server "echo_server.cpp")


target_link_libraries(echo_server PRIVATE Threads::Threads)