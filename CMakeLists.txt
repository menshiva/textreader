cmake_minimum_required(VERSION 3.28)
project(TextReader CXX)

set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # static CRT

file(GLOB_RECURSE SOURCES src/*)
add_executable(TextReader ${SOURCES})
