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
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME HostControllerA)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Select which firmware image to build out of this single project.
# Each variant produces its own binary (see User/Src/<variant> and User/Inc/<variant>).
set(FIRMWARE_VARIANT "HostController" CACHE STRING "Firmware variant to build (HostController or DisplayController)")
set_property(CACHE FIRMWARE_VARIANT PROPERTY STRINGS HostController DisplayController)
if(NOT FIRMWARE_VARIANT STREQUAL "HostController" AND NOT FIRMWARE_VARIANT STREQUAL "DisplayController")
    message(FATAL_ERROR "FIRMWARE_VARIANT must be 'HostController' or 'DisplayController' (got '${FIRMWARE_VARIANT}')")
endif()
message("Firmware variant: " ${FIRMWARE_VARIANT})

# Enable CMake support for ASM, C and C++ languages
enable_language(C CXX ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
# Sources under User/Src/HostController and User/Src/DisplayController are variant-specific;
# everything else under User/Src is shared by both firmware images.
file(GLOB_RECURSE USER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/User/Src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/Src/*.cpp
)
list(FILTER USER_SOURCES EXCLUDE REGEX "/User/Src/(HostController|DisplayController)/")

file(GLOB_RECURSE USER_SOURCES_VARIANT CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/User/Src/${FIRMWARE_VARIANT}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/User/Src/${FIRMWARE_VARIANT}/*.cpp
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${USER_SOURCES}
    ${USER_SOURCES_VARIANT}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Appli/App
    ${CMAKE_CURRENT_SOURCE_DIR}/User/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/User/Inc/${FIRMWARE_VARIANT}
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    FIRMWARE_VARIANT_${FIRMWARE_VARIANT}
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
