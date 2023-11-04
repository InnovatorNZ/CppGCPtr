cmake_minimum_required(VERSION 3.15)
project(CppGCPtr)
set(CMAKE_CXX_STANDARD 20)

cmake_host_system_information(RESULT CPU_NUMBER_OF_LOGICAL_CORES QUERY NUMBER_OF_LOGICAL_CORES)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP${CPU_NUMBER_OF_LOGICAL_CORES}>)

file(GLOB_RECURSE HDRS "*.h")
aux_source_directory("." SRCS)
add_executable(CppGCPtr ${SRCS} ${HDRS} CppExecutor/ThreadPoolExecutor.cpp)