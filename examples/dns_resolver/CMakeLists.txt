cmake_minimum_required (VERSION 3.8)

add_executable(dns_resolver "dns_resolver.cpp")


target_link_libraries(dns_resolver PRIVATE Threads::Threads)
