cmake_minimum_required (VERSION 3.8)

add_executable(chatroom_server "chatroom_server.cpp")

target_link_libraries(chatroom_server PRIVATE Threads::Threads)
