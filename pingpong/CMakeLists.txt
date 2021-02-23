cmake_minimum_required (VERSION 3.8)

add_executable(pingpong_client "pingpong_client.cpp")
add_executable(pingpong_server "pingpong_server.cpp")
add_executable(pingpong_server_mult "pingpong_server_mult.cpp")
add_executable(pingpong_server_spawn "pingpong_server_spawn.cpp")

target_link_libraries(pingpong_server_mult PRIVATE Threads::Threads)

