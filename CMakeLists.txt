cmake_minimum_required (VERSION 3.8)

project ("tinycoro")


if (MSVC)
    # warning level 4 and all warnings as errors
    add_compile_options(/std:c++latest)
else()
    # lots of warnings and all warnings as errors
    add_compile_options(-fcoroutines -std=c++2a -Wfatal-errors)
endif()

add_subdirectory("echo_server")
add_subdirectory ("sleepsort")
