cmake_minimum_required(VERSION 3.22)

# Code shared by the HostControllerA and DisplayController firmware. The
# firmware projects compile it themselves through CommonSources.cmake; this
# project only builds the native unit tests in tests/ with the host compiler
# (see HostControllerA/docs/Testing.md).

project(Common LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

enable_testing()
add_subdirectory(tests)
