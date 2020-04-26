# Sets the minimum version of CMake required to build your native library.
# This ensures that a certain set of CMake features is available to
# your build.

cmake_minimum_required(VERSION 3.4.1)

# Specifies a path to native header files.
include_directories(src/main/cpp/)

add_library(proxy SHARED src/main/cpp/proxy.cpp)
add_library(proxy_setting STATIC src/main/cpp/proxy_setting.cpp)
add_library(tools STATIC src/main/cpp/tools.cpp)
add_library(kernel_info STATIC src/main/cpp/kernel_info.cpp)
add_library(hints STATIC src/main/cpp/hints.cpp)
add_library(subflow STATIC src/main/cpp/subflow.cpp)
add_library(connections STATIC src/main/cpp/connections.cpp)
add_library(pipe STATIC src/main/cpp/pipe.cpp)
target_link_libraries(subflow android log connections hints kernel_info)
target_link_libraries(proxy connections subflow tools proxy_setting pipe)
target_link_libraries(connections android log tools subflow kernel_info)
