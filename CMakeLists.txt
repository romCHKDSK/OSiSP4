cmake_minimum_required(VERSION 3.0)

project(lr4 LANGUAGES C)

add_executable(lr4 main.c)

install(TARGETS lr4 RUNTIME DESTINATION bin)
