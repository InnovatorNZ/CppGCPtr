cmake_minimum_required(VERSION 3.18)
project(CppGCPtr)
set(CMAKE_CXX_STANDARD 20)

include_directories(.)
aux_source_directory("." SRCS)
add_executable(CppGCPtr ${SRCS} ${HDRS})