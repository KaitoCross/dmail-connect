cmake_minimum_required(VERSION 3.10)
project(datechange_distserver)

set(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_FLAGS -pthread)
add_executable(datechange_distserver main.cpp)