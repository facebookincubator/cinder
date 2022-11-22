cmake_minimum_required(VERSION 3.5)

project (valgrind)

add_library(valgrind INTERFACE)
target_include_directories(valgrind INTERFACE include)
