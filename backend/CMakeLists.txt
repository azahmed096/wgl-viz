cmake_minimum_required(VERSION 3.12)
project(libweb C)

set(CMAKE_C_STANDARD 99)

add_executable(libweb main.c)
target_link_libraries(libweb websockets)
target_link_libraries(libweb pthread)
