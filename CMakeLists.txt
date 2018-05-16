cmake_minimum_required(VERSION 3.10)
project(offsetForwarder)

set(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_FLAGS -pthread)
add_executable(offsetForwarder main.cpp SignalHandling.cpp SignalHandling.h)