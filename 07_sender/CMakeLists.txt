cmake_minimum_required(VERSION 3.13)

project(lwm2m_sender C)

add_executable(sender sender.c)
target_compile_definitions(sender PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(sender PRIVATE -Wall -Wextra -Werror -pedantic)