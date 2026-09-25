cmake_minimum_required(VERSION 3.22)

# Native unit tests for the shared code. board/main.h stands in for a firmware
# project's Core/Inc/main.h, with the pin labels the shared code uses. See
# HostControllerA/docs/Testing.md.

set(NATIVE_TEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(NATIVE_TEST_INCLUDE_DIRS)
set(NATIVE_TEST_BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/board)
include(NativeTest.cmake)

# Display
add_native_test(numeric_display_tests
    SOURCES NumericDisplayTests.cpp
    SUT Src/Display/DisplayTypes.cpp
)
add_native_test(display_codec_tests
    SOURCES DisplayCodecTests.cpp
    SUT Src/Display/DisplayCodec.cpp
)
add_native_test(display_i2c_protocol_tests
    SOURCES DisplayI2cProtocolTests.cpp
    SUT Src/Display/DisplayI2cProtocol.cpp
)
add_native_test(display_address_tests
    SOURCES DisplayAddressTests.cpp
    SUT Src/Display/DisplayAddress.cpp
    STUBS
)

# Utils
add_native_test(crc32_tests
    SOURCES Crc32Tests.cpp
)
