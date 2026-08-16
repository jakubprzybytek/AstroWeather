cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Bypass)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

set(ST67_DEFAULT_MODE "MANUFACTURE" CACHE STRING
    "ST67 startup mode: MANUFACTURE or BOOTLOADER")
set_property(CACHE ST67_DEFAULT_MODE PROPERTY STRINGS MANUFACTURE BOOTLOADER)

if(ST67_DEFAULT_MODE STREQUAL "MANUFACTURE")
    set(ST67_DEFAULT_MODE_DEFINE ST67_DEFAULT_MODE_MANUFACTURE)
elseif(ST67_DEFAULT_MODE STREQUAL "BOOTLOADER")
    set(ST67_DEFAULT_MODE_DEFINE ST67_DEFAULT_MODE_BOOTLOADER)
else()
    message(FATAL_ERROR "Unsupported ST67_DEFAULT_MODE: ${ST67_DEFAULT_MODE}")
endif()

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    User/Src/bridge.c
    User/Src/st67_mode.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    User/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    ST67_DEFAULT_MODE=${ST67_DEFAULT_MODE_DEFINE}
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
