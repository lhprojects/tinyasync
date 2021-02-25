cmake_minimum_required (VERSION 3.8)

add_executable(http_client "http_client.cpp")


target_link_libraries(http_client PRIVATE Threads::Threads)
